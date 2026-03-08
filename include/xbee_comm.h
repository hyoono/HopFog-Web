/*
 * xbee_comm.h — XBee S2C API Mode 1 driver (rebuilt from scratch).
 *
 * Based on the working XBEE_COMM_TEST project that was proven to
 * communicate between two ESP32-CAM boards over XBee ZigBee.
 *
 * XBee configuration:
 *   AP = 1 (API mode without escapes)
 *   CE = 1 (Coordinator) on admin,  CE = 0 (Router) on nodes
 *   BD = 3 (9600 baud)
 *   ID = same PAN ID on all modules
 *
 * ESP32-CAM wiring:
 *   GPIO 1 (U0TXD) → XBee DIN  (pin 3)
 *   GPIO 3 (U0RXD) ← XBee DOUT (pin 2)
 *   3.3V            → XBee VCC  (pin 1)
 *   GND             → XBee GND  (pin 10)
 *   Disconnect XBee before uploading firmware.
 */

#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── API Mode 1 frame types ──────────────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10
#define XBEE_TX_STATUS     0x8B
#define XBEE_MODEM_STATUS  0x8A
#define XBEE_RX_PACKET     0x90
#define XBEE_MAX_FRAME     512

// ── Event log ───────────────────────────────────────────────────────
#define XBEE_LOG_SIZE      50
#define XBEE_LOG_MSG_MAX   200

struct XBeeLogEntry {
    unsigned long ts;
    char direction;           // 'T','R','E','S'
    uint8_t frameType;
    uint8_t frameId;
    char msg[XBEE_LOG_MSG_MAX];
};

// ── Callback for received RF data ───────────────────────────────────
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

// ── Public API ──────────────────────────────────────────────────────
void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
void xbeeProcessIncoming();
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

/// Flush any pending RX bytes (call after SD/WiFi init to clear garbage).
void xbeeFlushRx();

// ── Web API (for admin testing page) ────────────────────────────────
void xbeeGetLog(JsonArray& arr);
unsigned long xbeeGetRxByteCount();
void xbeeGetDiagnostics(JsonObject& diag);
bool xbeeRunLoopbackTest(String& result);

#endif // XBEE_COMM_H
