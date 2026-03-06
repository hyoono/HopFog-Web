/*
 * xbee_comm.cpp — Digi XBee S2C (ZigBee) serial driver.
 *
 * Uses API mode 1 (AP=1) with binary-framed packets.
 * JSON payloads are wrapped in 0x10 Transmit Request frames for
 * sending, and extracted from 0x90 Receive Packet frames on receive.
 *
 * Always uses UART2 (Serial2) on GPIO 13/12 so that UART0 (Serial)
 * remains available for Serial Monitor debug output.
 *
 * Includes a ring-buffer event log that is exposed via /api/xbee/rx-log
 * so the web admin testing page can show a live "serial monitor".
 */

#include "xbee_comm.h"
#include "config.h"

// ── Internal state ──────────────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial2;
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;
static unsigned long   totalRxBytes   = 0;   // diagnostic counter

// Maximum API frame payload we'll accept (0x90 overhead + RF data)
#define XBEE_MAX_FRAME  512

// ── Event log ring buffer ───────────────────────────────────────────
static XBeeLogEntry logRing[XBEE_LOG_SIZE];
static int logHead = 0;   // next write index
static int logCount = 0;  // entries stored

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
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };

static RxState rxState  = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// ── Public API ──────────────────────────────────────────────────────

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    Serial.printf("[XBee] UART2 started (API mode 1) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
    logEvent('S', 0, 0, "XBee init: TX=GPIO%d RX=GPIO%d baud=%d",
             XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) {
        logEvent('E', 0x10, 0, "TX rejected: payload %d bytes (max %d)",
                 (int)len, XBEE_MAX_FRAME - 14);
        return 0;
    }

    uint16_t frameDataLen = 14 + len;

    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE,
        0x00, 0x00
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

    // Log a truncated preview of the payload (max ~170 chars to fit log entry)
    char preview[XBEE_LOG_MSG_MAX];
    snprintf(preview, sizeof(preview), "(%d B) %.170s%s",
             (int)len, payload, len > 170 ? "..." : "");
    logEvent('T', 0x10, fid, "%s", preview);

    Serial.printf("[XBee] TX frame ID=%d (%d bytes payload)\n", fid, (int)len);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        totalRxBytes++;

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) {
                rxState = GOT_LEN_HI;
            }
            break;

        case GOT_LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = GOT_LEN_LO;
            break;

        case GOT_LEN_LO:
            rxFrameLen |= b;
            rxIdx = 0;
            rxChecksum = 0;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) {
                logEvent('E', 0, 0, "Invalid frame length %d — reset", rxFrameLen);
                rxState = WAIT_DELIM;
            } else {
                rxState = READING_DATA;
            }
            break;

        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) {
                rxState = GOT_CHECKSUM;
            }
            break;

        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen >= 13) {
                    // Extract 64-bit source address for logging
                    char srcAddr[18];
                    snprintf(srcAddr, sizeof(srcAddr),
                             "%02X%02X%02X%02X%02X%02X%02X%02X",
                             rxFrame[1], rxFrame[2], rxFrame[3], rxFrame[4],
                             rxFrame[5], rxFrame[6], rxFrame[7], rxFrame[8]);

                    const char* rfData = (const char*)&rxFrame[12];
                    size_t rfLen = rxFrameLen - 12;

                    while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) {
                        rfLen--;
                    }

                    if (rfLen > 0 && rfLen < XBEE_MAX_FRAME - 12) {
                        rxFrame[12 + rfLen] = '\0';

                        // Log a truncated preview (max ~170 chars to fit log entry)
                        char preview[XBEE_LOG_MSG_MAX];
                        snprintf(preview, sizeof(preview),
                                 "from %s (%d B) %.170s%s",
                                 srcAddr, (int)rfLen, rfData,
                                 rfLen > 170 ? "..." : "");
                        logEvent('R', 0x90, 0, "%s", preview);

                        if (rxCallback) {
                            rxCallback(rfData, rfLen);
                        }
                    }
                } else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    uint8_t fid = rxFrame[1];
                    uint8_t delivery = rxFrame[5];
                    uint8_t retries = rxFrame[4];
                    if (delivery == 0) {
                        logEvent('S', 0x8B, fid, "TX OK (retries=%d)", retries);
                    } else {
                        logEvent('E', 0x8B, fid,
                                 "TX FAILED delivery=0x%02X retries=%d",
                                 delivery, retries);
                        Serial.printf("[XBee] TX status: frame %d delivery FAILED (0x%02X)\n",
                                      fid, delivery);
                    }
                } else {
                    logEvent('R', frameType, 0, "Unknown frame type 0x%02X len=%d",
                             frameType, rxFrameLen);
                }
            } else {
                logEvent('E', 0, 0, "Checksum error (expected 0xFF, got 0x%02X)",
                         rxChecksum);
                Serial.println("[XBee] RX frame checksum error — dropped");
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}

void xbeeGetLog(JsonArray& arr) {
    // Output newest first
    for (int i = 0; i < logCount; i++) {
        int idx = (logHead - 1 - i + XBEE_LOG_SIZE) % XBEE_LOG_SIZE;
        const XBeeLogEntry& e = logRing[idx];
        JsonObject o = arr.add<JsonObject>();
        o["ts"] = e.ts;
        o["dir"] = String(e.direction);
        o["frame_type"] = e.frameType;
        o["frame_id"] = e.frameId;
        o["msg"] = e.msg;
    }
}

unsigned long xbeeGetRxByteCount() {
    return totalRxBytes;
}
