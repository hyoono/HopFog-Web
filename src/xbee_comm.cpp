/*
 * xbee_comm.cpp — Digi XBee S2C (ZigBee) serial driver.
 *
 * Uses API mode 1 (AP=1) with binary-framed packets.
 * JSON payloads are wrapped in 0x10 Transmit Request frames for
 * sending, and extracted from 0x90 Receive Packet frames on receive.
 *
 * Always uses UART2 (Serial2) on GPIO 13/12 so that UART0 (Serial)
 * remains available for Serial Monitor debug output.
 */

#include "xbee_comm.h"
#include "config.h"

// ── Internal state ──────────────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial2;
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;

// Maximum API frame payload we'll accept (0x90 overhead + RF data)
#define XBEE_MAX_FRAME  512

// ── Frame receive state machine ─────────────────────────────────────
// API mode 1 frame: 0x7E | LenHi | LenLo | <frame data ...> | Checksum
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };

static RxState rxState  = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;   // expected frame-data length
static uint16_t rxIdx       = 0;   // bytes of frame data received so far
static uint8_t  rxFrame[XBEE_MAX_FRAME]; // frame data buffer (type + payload)
static uint8_t  rxChecksum  = 0;   // running checksum accumulator

// ── Public API ──────────────────────────────────────────────────────

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    Serial.printf("[XBee] UART2 started (API mode 1) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    /*
     * Build a 0x10 Transmit Request frame:
     *
     *   0x7E                       — start delimiter
     *   LenHi  LenLo              — length of frame data
     *   0x10                       — frame type: Transmit Request
     *   FrameID                    — sequence number (1-255)
     *   64-bit dest (8 bytes)      — 0x000000000000FFFF (broadcast)
     *   16-bit dest (2 bytes)      — 0xFFFE (broadcast)
     *   Broadcast radius (1 byte)  — 0x00
     *   Options (1 byte)           — 0x00
     *   RF Data (N bytes)          — the JSON payload
     *   Checksum (1 byte)          — 0xFF minus sum of frame data
     *
     * Frame data length = 1 + 1 + 8 + 2 + 1 + 1 + len = 14 + len
     */

    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;

    // Rolling frame ID (1-255, 0 means "no response requested")
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    // Build frame data in a temporary buffer
    uint8_t hdr[14] = {
        XBEE_TX_REQUEST,            // [0]  frame type
        fid,                        // [1]  frame ID
        0x00, 0x00, 0x00, 0x00,     // [2-5]  64-bit dest high
        0x00, 0x00, 0xFF, 0xFF,     // [6-9]  64-bit dest low (broadcast)
        0xFF, 0xFE,                 // [10-11] 16-bit dest (broadcast)
        0x00,                       // [12] broadcast radius
        0x00                        // [13] options
    };

    // Compute checksum over header + payload
    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Write the frame to serial
    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));   // length MSB
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF)); // length LSB
    xbeeSerial.write(hdr, 14);
    xbeeSerial.write((const uint8_t*)payload, len);
    xbeeSerial.write(cksum);
    xbeeSerial.flush();

    Serial.printf("[XBee] TX frame ID=%d (%d bytes payload)\n", fid, (int)len);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();

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
                // Invalid length — reset
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
            // Verify: sum of all frame-data bytes + checksum byte = 0xFF
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                // Valid frame — process by type
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen >= 13) {
                    /*
                     * 0x90 Receive Packet:
                     *   [0]     frame type (0x90)
                     *   [1-8]   64-bit source address
                     *   [9-10]  16-bit source address
                     *   [11]    receive options
                     *   [12..]  RF data (the payload) — at least 1 byte
                     */
                    const char* rfData = (const char*)&rxFrame[12];
                    size_t rfLen = rxFrameLen - 12;

                    // Strip trailing newline/CR from payload if present
                    while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) {
                        rfLen--;
                    }

                    if (rfLen > 0 && rfLen < XBEE_MAX_FRAME - 12 && rxCallback) {
                        // Null-terminate for safe string use (bounds-checked)
                        rxFrame[12 + rfLen] = '\0';
                        rxCallback(rfData, rfLen);
                    }
                } else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    /*
                     * 0x8B Transmit Status:
                     *   [0]  frame type (0x8B)
                     *   [1]  frame ID
                     *   [2-3] 16-bit dest
                     *   [4]  retry count
                     *   [5]  delivery status (0 = success)
                     *   [6]  discovery status
                     */
                    uint8_t fid = rxFrame[1];
                    uint8_t delivery = rxFrame[5];
                    if (delivery != 0) {
                        Serial.printf("[XBee] TX status: frame %d delivery FAILED (0x%02X)\n",
                                      fid, delivery);
                    }
                }
                // Other frame types are silently ignored
            } else {
                Serial.println("[XBee] RX frame checksum error — dropped");
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
