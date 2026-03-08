/*
 * xbee_comm.h — XBee S2C API Mode 1 driver.
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
#define XBEE_AT_COMMAND    0x08
#define XBEE_AT_RESPONSE   0x88
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

// ── XBee module configuration (queried via AT commands) ─────────────
struct XBeeConfig {
    bool    valid;            // true if at least one AT response received
    int     ap_mode;          // AP parameter (1 = API mode 1, 0 = transparent)
    int     coordinator;      // CE parameter (1 = coordinator, 0 = router)
    uint16_t pan_id;          // ID parameter (PAN ID)
    uint16_t my_addr;         // MY parameter (16-bit network address)
    int     responses;        // how many AT responses received (expect 4)
};

// ── Callback for received RF data ───────────────────────────────────
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

// ── Public API ──────────────────────────────────────────────────────
void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
void xbeeProcessIncoming();
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

/// Query XBee module config via AT Command frames (AP, ID, CE, MY).
/// Call AFTER xbeeInit(). Takes ~500ms. Results available via xbeeGetConfig().
void xbeeQueryConfig();

/// Get cached XBee config from last xbeeQueryConfig() call.
const XBeeConfig& xbeeGetConfig();

// ── Web API (for admin testing page) ────────────────────────────────
void xbeeGetLog(JsonArray& arr);
unsigned long xbeeGetRxByteCount();
void xbeeGetDiagnostics(JsonObject& diag);
bool xbeeRunLoopbackTest(String& result);

#endif // XBEE_COMM_H
