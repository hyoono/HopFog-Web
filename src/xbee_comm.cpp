// ═══════════════════════════════════════════════════════════════════════
//  xbee_comm.cpp — XBee S2C API Mode 1 driver
//
//  Key feature: AT Command probe (xbeeQueryConfig) that queries the
//  XBee module's AP, ID, CE, MY parameters via API frames.  If we get
//  a response, UART TX + RX are both proven working.
// ═══════════════════════════════════════════════════════════════════════

#include "xbee_comm.h"
#include "config.h"
#include <stdarg.h>

// ── UART handle ─────────────────────────────────────────────────────
#ifdef XBEE_USES_UART0
  static HardwareSerial& xbeeSerial = Serial;
#else
  static HardwareSerial& xbeeSerial = Serial2;
#endif

// ── Callback ────────────────────────────────────────────────────────
static XBeeReceiveCB rxCallback = nullptr;

// ── TX frame ID counter (1–255, wraps) ──────────────────────────────
static uint8_t frameIdCounter = 0;

// ── Diagnostic counters ─────────────────────────────────────────────
static unsigned long totalRxBytes    = 0;
static unsigned long totalTxBytes    = 0;
static unsigned long txStatusOk      = 0;
static unsigned long txStatusFail    = 0;
static unsigned long rxFramesParsed  = 0;
static unsigned long rxDataFrames    = 0;
static unsigned long selfEchoCount   = 0;
static unsigned long checksumErrors  = 0;
static unsigned long frameTimeouts   = 0;
static unsigned long modemStatusCnt  = 0;
static uint8_t       lastModemStatus = 0;

// ── XBee module configuration (populated by xbeeQueryConfig) ────────
static XBeeConfig xbeeConfig = {};

// ── Event log ring buffer ───────────────────────────────────────────
static XBeeLogEntry logRing[XBEE_LOG_SIZE];
static int logHead  = 0;
static int logCount = 0;

static void logEvent(char dir, uint8_t ftype, uint8_t fid, const char* fmt, ...) {
    XBeeLogEntry& e = logRing[logHead];
    e.ts = millis();
    e.direction = dir;
    e.frameType = ftype;
    e.frameId = fid;
    va_list args;
    va_start(args, fmt);
    vsnprintf(e.msg, XBEE_LOG_MSG_MAX, fmt, args);
    va_end(args);
    logHead = (logHead + 1) % XBEE_LOG_SIZE;
    if (logCount < XBEE_LOG_SIZE) logCount++;
}

// ── Frame receive state machine ─────────────────────────────────────
enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };

static RxState   rxState      = WAIT_DELIM;
static uint16_t  rxFrameLen   = 0;
static uint16_t  rxIdx        = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum   = 0;
static uint32_t  rxFrameStart = 0;

static const uint32_t FRAME_TIMEOUT_MS = 1000;

// ═════════════════════════════════════════════════════════════════════
//  xbeeInit — configure UART for XBee at 9600 baud
// ═════════════════════════════════════════════════════════════════════

void xbeeInit() {
#ifdef XBEE_USES_UART0
    xbeeSerial.begin(XBEE_BAUD);
#else
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
#endif

    // Flush bootloader garbage (115200→9600 baud change produces noise).
    {
        uint32_t lastByte = millis();
        while (millis() - lastByte < 100) {
            if (xbeeSerial.available()) {
                xbeeSerial.read();
                lastByte = millis();
            }
        }
    }

    memset(&xbeeConfig, 0, sizeof(xbeeConfig));
    xbeeConfig.ap_mode = -1;
    xbeeConfig.coordinator = -1;

    logEvent('S', 0, 0, "XBee init: TX=GPIO%d RX=GPIO%d baud=%d",
             XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
    dbgprintf("[XBee] UART ready — TX=GPIO%d RX=GPIO%d baud=%d\n",
              XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeSendBroadcast — build and send a 0x10 Transmit Request frame
// ═════════════════════════════════════════════════════════════════════

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE, 0x00, 0x00
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF));
    xbeeSerial.write(hdr, 14);
    xbeeSerial.write((const uint8_t*)payload, len);
    xbeeSerial.write(cksum);
    xbeeSerial.flush();

    totalTxBytes += 3 + 14 + len + 1;

    logEvent('T', 0x10, fid, "(%d B) %.170s%s",
             (int)len, payload, len > 170 ? "..." : "");
    dbgprintf("[XBee] TX id=%d len=%d\n", fid, (int)len);
    return fid;
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeSetReceiveCallback
// ═════════════════════════════════════════════════════════════════════

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

// ═════════════════════════════════════════════════════════════════════
//  sendATQuery — send an AT Command frame (0x08) to query a parameter
// ═════════════════════════════════════════════════════════════════════

static void sendATQuery(uint8_t fid, char at1, char at2) {
    uint8_t frame[8];
    frame[0] = XBEE_START_DELIM;
    frame[1] = 0x00;  // length MSB
    frame[2] = 0x04;  // length LSB (type + fid + at1 + at2 = 4)
    frame[3] = XBEE_AT_COMMAND;
    frame[4] = fid;
    frame[5] = (uint8_t)at1;
    frame[6] = (uint8_t)at2;
    frame[7] = 0xFF - ((frame[3] + frame[4] + frame[5] + frame[6]) & 0xFF);

    xbeeSerial.write(frame, 8);
    xbeeSerial.flush();
    totalTxBytes += 8;
}

// ═════════════════════════════════════════════════════════════════════
//  handleCompleteFrame — dispatch a validated API frame
// ═════════════════════════════════════════════════════════════════════

static void handleCompleteFrame() {
    rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    switch (ft) {

    // ── 0x90  Receive Packet (RF data from remote XBee) ─────────
    case XBEE_RX_PACKET:
        if (rxFrameLen < 13) break;
        {
            const char* rfData = (const char*)&rxFrame[12];
            size_t rfLen = rxFrameLen - 12;

            while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r'))
                rfLen--;

            if (rfLen > 0 && rfLen < XBEE_MAX_FRAME - 12) {
                rxFrame[12 + rfLen] = '\0';
                rxDataFrames++;

                logEvent('R', 0x90, 0, "(%d B) %.170s%s",
                         (int)rfLen, rfData, rfLen > 170 ? "..." : "");
                dbgprintf("[XBee] RX 0x90 (%d B): %.80s\n", (int)rfLen, rfData);

                if (rxCallback) rxCallback(rfData, rfLen);
            }
        }
        break;

    // ── 0x8B  Transmit Status ───────────────────────────────────
    case XBEE_TX_STATUS:
        if (rxFrameLen < 7) break;
        {
            uint8_t fid = rxFrame[1];
            uint8_t delivery = rxFrame[5];
            if (delivery == 0x00) {
                txStatusOk++;
                logEvent('S', 0x8B, fid, "TX OK");
            } else {
                txStatusFail++;
                logEvent('E', 0x8B, fid, "TX FAIL 0x%02X", delivery);
            }
        }
        break;

    // ── 0x8A  Modem Status ──────────────────────────────────────
    case XBEE_MODEM_STATUS:
        if (rxFrameLen >= 2) {
            modemStatusCnt++;
            lastModemStatus = rxFrame[1];
            const char* desc = "unknown";
            switch (rxFrame[1]) {
                case 0x00: desc = "Hardware reset"; break;
                case 0x01: desc = "Watchdog reset"; break;
                case 0x02: desc = "Joined network"; break;
                case 0x03: desc = "Disassociated"; break;
                case 0x06: desc = "Coordinator started"; break;
                case 0x0D: desc = "Network key updated"; break;
                case 0x11: desc = "Config changed (re-applying)"; break;
            }
            logEvent('S', 0x8A, 0, "Modem: %s (0x%02X)", desc, rxFrame[1]);
        }
        break;

    // ── 0x88  AT Command Response ───────────────────────────────
    case XBEE_AT_RESPONSE:
        if (rxFrameLen >= 5) {
            char at[3] = { (char)rxFrame[2], (char)rxFrame[3], '\0' };
            uint8_t status = rxFrame[4];
            if (status == 0x00) {
                xbeeConfig.responses++;
                xbeeConfig.valid = true;

                // AP and CE are 1-byte values.
                // ID and MY are 1-byte if value < 0x100, 2-byte otherwise.
                if (at[0] == 'A' && at[1] == 'P' && rxFrameLen >= 6) {
                    xbeeConfig.ap_mode = rxFrame[5];
                    logEvent('S', 0x88, rxFrame[1], "AT AP = %d", rxFrame[5]);
                }
                else if (at[0] == 'I' && at[1] == 'D' && rxFrameLen >= 6) {
                    xbeeConfig.pan_id = (rxFrameLen >= 7)
                        ? ((uint16_t)rxFrame[5] << 8) | rxFrame[6]
                        : rxFrame[5];
                    logEvent('S', 0x88, rxFrame[1], "AT ID = 0x%04X", xbeeConfig.pan_id);
                }
                else if (at[0] == 'C' && at[1] == 'E' && rxFrameLen >= 6) {
                    xbeeConfig.coordinator = rxFrame[5];
                    logEvent('S', 0x88, rxFrame[1], "AT CE = %d (%s)",
                             rxFrame[5], rxFrame[5] ? "Coordinator" : "Router");
                }
                else if (at[0] == 'M' && at[1] == 'Y' && rxFrameLen >= 6) {
                    xbeeConfig.my_addr = (rxFrameLen >= 7)
                        ? ((uint16_t)rxFrame[5] << 8) | rxFrame[6]
                        : rxFrame[5];
                    logEvent('S', 0x88, rxFrame[1], "AT MY = 0x%04X", xbeeConfig.my_addr);
                }
                else {
                    logEvent('S', 0x88, rxFrame[1], "AT %s OK", at);
                }
            } else {
                logEvent('E', 0x88, rxFrame[1], "AT %s FAIL status=0x%02X", at, status);
            }
        }
        break;

    // ── 0x10  Self-echo (our own TX looping back) ───────────────
    case XBEE_TX_REQUEST:
        selfEchoCount++;
        break;

    default:
        logEvent('R', ft, 0, "Unknown frame 0x%02X len=%d", ft, rxFrameLen);
        break;
    }
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeProcessIncoming — call from loop(), feeds bytes into the parser
// ═════════════════════════════════════════════════════════════════════

void xbeeProcessIncoming() {
    if (rxState != WAIT_DELIM) {
        if (millis() - rxFrameStart > FRAME_TIMEOUT_MS) {
            frameTimeouts++;
            logEvent('E', 0, 0, "Frame timeout (state=%d, %d/%d bytes)",
                     rxState, rxIdx, rxFrameLen);
            rxState = WAIT_DELIM;
        }
    }

    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        totalRxBytes++;

        switch (rxState) {

        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) {
                rxState = LEN_HI;
                rxFrameStart = millis();
            }
            break;

        case LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = LEN_LO;
            break;

        case LEN_LO:
            rxFrameLen |= b;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) {
                logEvent('E', 0, 0, "Bad frame len %d — reset", rxFrameLen);
                rxState = WAIT_DELIM;
            } else {
                rxIdx = 0;
                rxChecksum = 0;
                rxState = FRAME_DATA;
            }
            break;

        case FRAME_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = CHECKSUM;
            break;

        case CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                handleCompleteFrame();
            } else {
                checksumErrors++;
                logEvent('E', 0, 0, "Checksum error 0x%02X", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeQueryConfig — query XBee module via AT Command frames
// ═════════════════════════════════════════════════════════════════════
//
//  Sends AT queries for AP, ID, CE, MY.  Each query is 8 bytes and the
//  response takes ~10-50ms.  Total time: ~500ms.
//
//  If ANY response is received, it proves:
//    ✅ UART TX (ESP32 → XBee DIN) works
//    ✅ UART RX (XBee DOUT → ESP32) works
//    ✅ XBee module is powered and responsive
//
//  The results are cached and available via xbeeGetConfig().

void xbeeQueryConfig() {
    logEvent('S', 0, 0, "Querying XBee module config (AT AP, ID, CE, MY)...");

    // Query AP (API mode) — frame ID 0xF1
    sendATQuery(0xF1, 'A', 'P');
    delay(100);
    xbeeProcessIncoming();

    // Query ID (PAN ID) — frame ID 0xF2
    sendATQuery(0xF2, 'I', 'D');
    delay(100);
    xbeeProcessIncoming();

    // Query CE (Coordinator Enable) — frame ID 0xF3
    sendATQuery(0xF3, 'C', 'E');
    delay(100);
    xbeeProcessIncoming();

    // Query MY (16-bit address) — frame ID 0xF4
    sendATQuery(0xF4, 'M', 'Y');
    delay(100);
    xbeeProcessIncoming();

    if (xbeeConfig.valid) {
        logEvent('S', 0, 0, "XBee probe OK: %d/4 responses. AP=%d CE=%d ID=0x%04X MY=0x%04X",
                 xbeeConfig.responses, xbeeConfig.ap_mode, xbeeConfig.coordinator,
                 xbeeConfig.pan_id, xbeeConfig.my_addr);
    } else {
        logEvent('E', 0, 0, "XBee probe FAILED: 0 responses. Check wiring and XBee power.");
    }
}

const XBeeConfig& xbeeGetConfig() {
    return xbeeConfig;
}

// ═════════════════════════════════════════════════════════════════════
//  Web API functions (for admin testing page)
// ═════════════════════════════════════════════════════════════════════

void xbeeGetLog(JsonArray& arr) {
    for (int i = 0; i < logCount; i++) {
        int idx = (logHead - 1 - i + XBEE_LOG_SIZE) % XBEE_LOG_SIZE;
        const XBeeLogEntry& e = logRing[idx];
        JsonObject o = arr.add<JsonObject>();
        o["ts"]         = e.ts;
        o["dir"]        = String(e.direction);
        o["frame_type"] = e.frameType;
        o["frame_id"]   = e.frameId;
        o["msg"]        = e.msg;
    }
}

unsigned long xbeeGetRxByteCount() {
    return totalRxBytes;
}

void xbeeGetDiagnostics(JsonObject& diag) {
    diag["tx_bytes"]         = totalTxBytes;
    diag["rx_bytes"]         = totalRxBytes;
    diag["rx_frames_parsed"] = rxFramesParsed;
    diag["rx_data_frames"]   = rxDataFrames;
    diag["tx_status_ok"]     = txStatusOk;
    diag["tx_status_fail"]   = txStatusFail;
    diag["self_echo_count"]  = selfEchoCount;
    diag["checksum_errors"]  = checksumErrors;
    diag["frame_timeouts"]   = frameTimeouts;
    diag["modem_status_cnt"] = modemStatusCnt;
    diag["last_modem_status"] = lastModemStatus;
    diag["uptime_ms"]        = millis();
    diag["tx_pin"]           = XBEE_TX_PIN;
    diag["rx_pin"]           = XBEE_RX_PIN;
    diag["baud"]             = XBEE_BAUD;

    // XBee module config from AT query
    JsonObject cfg = diag["xbee_config"].to<JsonObject>();
    cfg["probe_ok"]    = xbeeConfig.valid;
    cfg["responses"]   = xbeeConfig.responses;
    cfg["ap_mode"]     = xbeeConfig.ap_mode;
    cfg["coordinator"] = xbeeConfig.coordinator;
    cfg["pan_id"]      = xbeeConfig.pan_id;
    cfg["my_addr"]     = xbeeConfig.my_addr;

    // Pan ID as hex string for display
    char panHex[8];
    snprintf(panHex, sizeof(panHex), "0x%04X", xbeeConfig.pan_id);
    cfg["pan_id_hex"] = panHex;

    char myHex[8];
    snprintf(myHex, sizeof(myHex), "0x%04X", xbeeConfig.my_addr);
    cfg["my_addr_hex"] = myHex;

    // Assessment
    if (!xbeeConfig.valid) {
        diag["assessment"] = "XBEE NOT RESPONDING — AT command probe got 0 replies. "
                             "Check: (1) XBee powered? (2) GPIO1→DIN, GPIO3←DOUT wired? "
                             "(3) XBee in AP=1 mode?";
    } else if (xbeeConfig.ap_mode != 1) {
        char buf[120];
        snprintf(buf, sizeof(buf),
                 "WRONG MODE — XBee AP=%d (need AP=1). "
                 "Open XCTU, set AP=1 (API mode without escapes), Write.",
                 xbeeConfig.ap_mode);
        diag["assessment"] = buf;
    } else if (rxDataFrames > 0) {
        diag["assessment"] = "FULLY WORKING — XBee AP=1, receiving data from remote node.";
    } else if (txStatusOk > 0 && totalRxBytes > 0) {
        diag["assessment"] = "UART OK, TX OK — waiting for remote node to send data. "
                             "Check node is powered and has same PAN ID.";
    } else if (txStatusFail > 0) {
        diag["assessment"] = "TX FAIL — XBee sends but delivery fails. "
                             "No remote device found. Check PAN IDs match.";
    } else if (totalRxBytes > 0) {
        diag["assessment"] = "UART RX OK but no parsed frames yet. Waiting...";
    } else {
        diag["assessment"] = "UART OK (AT probe passed) but no data yet. Waiting for TX/RX...";
    }
}

bool xbeeRunLoopbackTest(String& result) {
    XBeeReceiveCB savedCb = rxCallback;
    rxCallback = nullptr;
    while (xbeeSerial.available()) xbeeSerial.read();

    const uint8_t pattern[] = { 0xAA, 0x55, 0xBB, 0x42 };
    xbeeSerial.write(pattern, 4);
    xbeeSerial.flush();
    delay(50);

    int readBack = 0;
    bool match = true;
    while (xbeeSerial.available() && readBack < 4) {
        uint8_t b = xbeeSerial.read();
        if (b != pattern[readBack]) match = false;
        readBack++;
    }
    rxCallback = savedCb;

    if (readBack == 4 && match) {
        result = "PASS — UART loopback OK (4/4 bytes). Remove jumper, reconnect XBee.";
        return true;
    } else if (readBack > 0) {
        result = "PARTIAL — read " + String(readBack) + "/4 bytes (mismatch).";
        return false;
    } else {
        result = "No bytes read back. Normal if XBee is connected. "
                 "To test UART: disconnect XBee, bridge GPIO " + String(XBEE_TX_PIN) +
                 " → GPIO " + String(XBEE_RX_PIN) + " with a jumper wire.";
        return false;
    }
}
