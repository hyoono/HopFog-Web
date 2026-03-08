// ═══════════════════════════════════════════════════════════════════════
//  xbee_comm.cpp — XBee S2C API Mode 1 driver (rebuilt from scratch)
//
//  Based on the working XBEE_COMM_TEST project that was proven to
//  communicate between two ESP32-CAM boards.
//
//  Key design decisions (matching the working test project):
//    - Serial.begin(9600) only — no Serial.end(), no pin args
//    - 6 separate Serial.write() calls for TX (identical to test project)
//    - Same RX state machine with frame timeout
//    - Flush bootloader garbage after init
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

// Abandon an incomplete frame if no new byte arrives within 1 second.
// XBee at 9600 baud ≈ 1.04 ms/byte; a 512-byte frame ≈ 530 ms.
static const uint32_t FRAME_TIMEOUT_MS = 1000;

// ═════════════════════════════════════════════════════════════════════
//  xbeeInit — configure UART for XBee at 9600 baud
// ═════════════════════════════════════════════════════════════════════

void xbeeInit() {
    // Match the working test project exactly:
    //   xbeeSerial.begin(XBEE_BAUD);
    // No Serial.end(), no delay before, no explicit pin args for UART0.
#ifdef XBEE_USES_UART0
    xbeeSerial.begin(XBEE_BAUD);
#else
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
#endif

    // Flush bootloader garbage (bootloader runs at 115200 baud, XBee at 9600).
    // Wait until no new bytes arrive for 100 ms.
    {
        uint32_t lastByte = millis();
        while (millis() - lastByte < 100) {
            if (xbeeSerial.available()) {
                xbeeSerial.read();
                lastByte = millis();
            }
        }
    }

    logEvent('S', 0, 0, "XBee init: TX=GPIO%d RX=GPIO%d baud=%d",
             XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
    dbgprintf("[XBee] UART ready — TX=GPIO%d RX=GPIO%d baud=%d\n",
              XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeSendBroadcast — build and send a 0x10 Transmit Request frame
// ═════════════════════════════════════════════════════════════════════
//
//  Uses the same 6 separate Serial.write() calls as the working test
//  project.  The previous "atomic buffer" approach was not proven to
//  work and may have different timing characteristics.

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    // 14-byte header: frame type + ID + 64-bit broadcast addr + options
    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,  // dest: broadcast
        0xFF, 0xFE,                                        // 16-bit: 0xFFFE
        0x00,                                              // broadcast radius
        0x00                                               // options
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // ── 6 separate writes — matches the working test project ────────
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
//  xbeeFlushRx — discard any pending bytes on the serial port
// ═════════════════════════════════════════════════════════════════════

void xbeeFlushRx() {
    rxState = WAIT_DELIM;
    while (xbeeSerial.available()) xbeeSerial.read();
    logEvent('S', 0, 0, "RX buffer flushed");
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

            // Strip trailing CR/LF
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
                dbgprintf("[XBee] TX FAIL 0x%02X (id=%d)\n", delivery, fid);
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
            dbgprintf("[XBee] Modem status: 0x%02X (%s)\n", rxFrame[1], desc);
        }
        break;

    // ── 0x10  Self-echo (our own TX looping back) ───────────────
    case XBEE_TX_REQUEST:
        selfEchoCount++;
        break;  // silently ignore

    default:
        logEvent('R', ft, 0, "Unknown frame 0x%02X len=%d", ft, rxFrameLen);
        break;
    }
}

// ═════════════════════════════════════════════════════════════════════
//  xbeeProcessIncoming — call from loop(), feeds bytes into the parser
// ═════════════════════════════════════════════════════════════════════

void xbeeProcessIncoming() {
    // Frame timeout: abandon incomplete frames if stalled
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

    // Assessment
    if (rxDataFrames > 0) {
        diag["assessment"] = "FULLY WORKING — receiving 0x90 data frames from remote XBee.";
    } else if (txStatusOk > 0 && totalRxBytes > 0) {
        diag["assessment"] = "PARTIAL — TX OK, RX bytes seen but no 0x90 data frames yet.";
    } else if (txStatusOk > 0) {
        diag["assessment"] = "TX ONLY — XBee acknowledges frames but no RX data. "
                             "Check remote XBee power, same PAN ID, and sending.";
    } else if (totalRxBytes > 0) {
        diag["assessment"] = "RX BYTES but no valid frames. Check XBee AP=1 (API mode).";
    } else if (totalTxBytes > 0) {
        diag["assessment"] = "SENT bytes but no TX status and no RX. "
                             "Check XBee AP=1 and TX→DIN wiring.";
    } else {
        diag["assessment"] = "NO COMMUNICATION — check wiring, XBee power, and AP=1.";
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
