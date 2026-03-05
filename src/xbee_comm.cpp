/*
 * xbee_comm.cpp — Digi XBee S2C (ZigBee) serial driver.
 *
 * Uses AT/transparent mode (AP=0) with newline-delimited JSON.
 * Compatible with HopFog-Node which also uses AT mode.
 *
 * Always uses UART2 (Serial2) on GPIO 13/12 so that UART0 (Serial)
 * remains available for Serial Monitor debug output.
 */

#include "xbee_comm.h"
#include "config.h"

// ── Internal state ──────────────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial2;
static XBeeReceiveCB   rxCallback = nullptr;

#define XBEE_MAX_LINE  512  // max line length (including JSON payload)

// Line-receive buffer
static char    rxLine[XBEE_MAX_LINE];
static size_t  rxIdx = 0;

// ── Public API ──────────────────────────────────────────────────────

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    Serial.printf("[XBee] UART2 started (AT mode) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    // Reserve 2 bytes: 1 for the trailing '\n' we append, 1 for safety margin
    if (len == 0 || len > XBEE_MAX_LINE - 2) return 0;

    // In AT mode: just write the text + newline.
    // XBee broadcasts automatically (DH=0, DL=FFFF configuration).
    xbeeSerial.write((const uint8_t*)payload, len);
    xbeeSerial.write('\n');
    xbeeSerial.flush();

    Serial.printf("[XBee] TX (%d bytes)\n", (int)len);
    return 1;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        char c = xbeeSerial.read();

        if (c == '\n') {
            // End of line — process the buffer
            if (rxIdx > 0) {
                rxLine[rxIdx] = '\0';

                // Trim trailing \r if present
                if (rxIdx > 0 && rxLine[rxIdx - 1] == '\r') {
                    rxLine[--rxIdx] = '\0';
                }

                if (rxIdx > 0 && rxCallback) {
                    rxCallback(rxLine, rxIdx);
                }
            }
            rxIdx = 0;
        } else if (c != '\r') {
            // Accumulate character (skip bare \r)
            if (rxIdx < XBEE_MAX_LINE - 1) {
                rxLine[rxIdx++] = c;
            }
            // If buffer overflows, discard excess characters
        }
    }
}
