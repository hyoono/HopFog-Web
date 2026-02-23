/*
 * xbee_comm.cpp — Digi XBee S2C (ZigBee) serial driver.
 *
 * Communicates using XBee API mode 1 (no escaping).
 * Supports transmit-request (0x10) and receive-packet (0x90) frames.
 *
 * Always uses UART2 (Serial2) on GPIO 13/12 so that UART0 (Serial)
 * remains available for Serial Monitor debug output.
 */

#include "xbee_comm.h"
#include "config.h"

// ── Internal state ──────────────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial2;
static uint8_t         frameIdCounter = 0;
static XBeeReceiveCB   rxCallback     = nullptr;

#define XBEE_MAX_PAYLOAD  200  // max RF data bytes per TX frame

// Receive-frame buffer
#define XBEE_BUF_SIZE 256
static uint8_t  rxBuf[XBEE_BUF_SIZE];
static uint16_t rxIdx     = 0;
static uint16_t rxExpLen  = 0;
static bool     rxInFrame = false;

// ── Helpers ─────────────────────────────────────────────────────────

static uint8_t nextFrameId() {
    frameIdCounter++;
    if (frameIdCounter == 0) frameIdCounter = 1; // 0 means "no ACK"
    return frameIdCounter;
}

static uint8_t checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return 0xFF - sum;
}

/// Write a complete API frame (start-delimiter + length + payload + checksum).
static void sendFrame(const uint8_t* payload, size_t len) {
    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(len >> 8));
    xbeeSerial.write((uint8_t)(len & 0xFF));
    xbeeSerial.write(payload, len);
    xbeeSerial.write(checksum(payload, len));
    xbeeSerial.flush();
}

// ── Public API ──────────────────────────────────────────────────────

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    Serial.printf("[XBee] UART2 started — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    return xbeeSendUnicast(XBEE_BROADCAST_ADDR64_HI,
                           XBEE_BROADCAST_ADDR64_LO,
                           payload, len);
}

uint8_t xbeeSendUnicast(uint32_t addrHi, uint32_t addrLo,
                        const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_PAYLOAD) return 0;

    uint8_t id = nextFrameId();

    // Build Transmit Request (frame type 0x10)
    // Header: type(1) + frameId(1) + addr64(8) + addr16(2) +
    //         broadcastRadius(1) + options(1) = 14 bytes
    size_t frameLen = 14 + len;
    uint8_t buf[14 + XBEE_MAX_PAYLOAD];

    buf[0]  = XBEE_TX_REQUEST;   // frame type
    buf[1]  = id;                // frame ID
    // 64-bit destination (big-endian)
    buf[2]  = (addrHi >> 24) & 0xFF;
    buf[3]  = (addrHi >> 16) & 0xFF;
    buf[4]  = (addrHi >>  8) & 0xFF;
    buf[5]  =  addrHi        & 0xFF;
    buf[6]  = (addrLo >> 24) & 0xFF;
    buf[7]  = (addrLo >> 16) & 0xFF;
    buf[8]  = (addrLo >>  8) & 0xFF;
    buf[9]  =  addrLo        & 0xFF;
    // 16-bit destination (use 0xFFFE for unknown / broadcast)
    buf[10] = (XBEE_BROADCAST_ADDR16 >> 8) & 0xFF;
    buf[11] =  XBEE_BROADCAST_ADDR16       & 0xFF;
    buf[12] = 0x00; // broadcast radius (0 = max hops)
    buf[13] = 0x00; // options (0 = default)
    memcpy(buf + 14, payload, len);

    sendFrame(buf, frameLen);
    Serial.printf("[XBee] TX frame id=%d  len=%d  to=%08X%08X\n",
                  id, (int)len, addrHi, addrLo);
    return id;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();

        if (!rxInFrame) {
            if (b == XBEE_START_DELIM) {
                rxInFrame = true;
                rxIdx     = 0;
                rxExpLen  = 0;
            }
            continue;
        }

        rxBuf[rxIdx++] = b;

        // Bytes 0-1: length (MSB, LSB)
        if (rxIdx == 2) {
            rxExpLen = ((uint16_t)rxBuf[0] << 8) | rxBuf[1];
            if (rxExpLen > XBEE_BUF_SIZE - 3) {
                // Frame too large — discard
                rxInFrame = false;
                continue;
            }
        }

        // We've read length(2) + payload(rxExpLen) + checksum(1)
        if (rxIdx >= 2 && rxIdx == rxExpLen + 3) {
            // Verify checksum (over payload bytes only: buf[2 .. 2+rxExpLen-1])
            uint8_t cs = checksum(rxBuf + 2, rxExpLen);
            uint8_t expected = rxBuf[rxExpLen + 2];

            if (cs == expected) {
                uint8_t frameType = rxBuf[2]; // first payload byte

                if (frameType == XBEE_RX_PACKET && rxCallback) {
                    // RX Packet (0x90):
                    //   [2]=type, [3-10]=addr64, [11-12]=addr16,
                    //   [13]=options, [14..]=data
                    if (rxExpLen > 12) {
                        // RX header inside payload: addr64(8) + addr16(2) +
                        // options(1) + type(1) = 12 bytes before RF data
                        uint32_t sHi = ((uint32_t)rxBuf[3]  << 24) |
                                       ((uint32_t)rxBuf[4]  << 16) |
                                       ((uint32_t)rxBuf[5]  <<  8) |
                                        (uint32_t)rxBuf[6];
                        uint32_t sLo = ((uint32_t)rxBuf[7]  << 24) |
                                       ((uint32_t)rxBuf[8]  << 16) |
                                       ((uint32_t)rxBuf[9]  <<  8) |
                                        (uint32_t)rxBuf[10];
                        size_t dataLen = rxExpLen - 12; // payload minus RX header
                        rxCallback(rxBuf + 14, dataLen, sHi, sLo);
                    }
                }
                else if (frameType == XBEE_TX_STATUS) {
                    uint8_t status = (rxExpLen >= 6) ? rxBuf[7] : 0xFF;
                    Serial.printf("[XBee] TX status frame=%d  status=0x%02X %s\n",
                                  rxBuf[3], status,
                                  status == 0 ? "OK" : "FAIL");
                }
            } else {
                Serial.println("[XBee] RX checksum mismatch — frame dropped");
            }

            rxInFrame = false;
        }

        // Guard against buffer overrun
        if (rxIdx >= XBEE_BUF_SIZE) {
            rxInFrame = false;
        }
    }
}
