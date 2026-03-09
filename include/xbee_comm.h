/*
 * xbee_comm.h — XBee S2C API Mode 1 driver (ESP32-CAM only)
 *
 * UART0 on GPIO 1/3.  No USB Serial Monitor.
 *
 * Wiring:
 *   GPIO 1 (U0TXD) → XBee DIN  (pin 3)
 *   GPIO 3 (U0RXD) ← XBee DOUT (pin 2)
 *   3.3V            → XBee VCC  (pin 1)
 *   GND             → XBee GND  (pin 10)
 *
 * IMPORTANT: ZigBee broadcast max RF payload is ~84 bytes (NP parameter).
 * Broadcast frames CANNOT be fragmented. Use xbeeSendTo() for unicast
 * which supports fragmentation up to ~255 bytes.
 */

#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── XBee API Mode 1 Constants ────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10
#define XBEE_RX_PACKET     0x90
#define XBEE_TX_STATUS     0x8B
#define XBEE_MODEM_STATUS  0x8A

#define XBEE_MAX_FRAME     512

// ZigBee broadcast max RF payload (NP parameter, typical ~84 bytes).
// Messages larger than this WILL BE SILENTLY DROPPED by the XBee module
// when sent as broadcast. Use unicast (xbeeSendTo) for larger payloads.
#define XBEE_BROADCAST_MAX  72   // Safe limit (below 84-byte NP)

// ZigBee unicast max safe payload (after fragmentation overhead)
#define XBEE_UNICAST_MAX   240
// Delay between chunked SYNC_DATA messages (ms)
#define SYNC_CHUNK_DELAY_MS 50

// ── Source address from last received 0x90 frame ─────────────
struct XBeeAddr {
    uint8_t  addr64[8];   // 64-bit source address
    uint16_t addr16;      // 16-bit network address
    bool     valid;       // true after first 0x90 frame received
};

// ── Stats ────────────────────────────────────────────────────
struct XBeeStats {
    uint32_t totalRxBytes;
    uint32_t totalTxBytes;
    uint32_t rxFramesParsed;
    uint32_t txFramesSent;
    uint32_t txStatusOK;
    uint32_t txStatusFail;
    uint32_t modemStatusCount;
    uint8_t  lastModemStatus;
    uint32_t oversizedDrops;   // payloads exceeding broadcast limit
};

// ── Message log (admin-only, for web dashboard) ──────────────
#define MSG_LOG_SIZE  60

struct MsgLogEntry {
    unsigned long ts;
    char dir;               // 'T'=TX, 'R'=RX, 'S'=status, 'E'=error
    char text[200];
};

// Callback: (payload, len, source_address)
typedef void (*XBeeReceiveCB)(const char* payload, size_t len,
                              const XBeeAddr& src);

void xbeeInit();

// Broadcast (max ~72 bytes payload). Larger payloads ARE DROPPED.
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
inline uint8_t xbeeSendBroadcast(const char* text) {
    return xbeeSendBroadcast(text, strlen(text));
}

// Unicast to a specific XBee address (supports fragmentation, ~255 bytes).
uint8_t xbeeSendTo(const XBeeAddr& dest, const char* payload, size_t len);

void xbeeSetReceiveCallback(XBeeReceiveCB cb);
void xbeeProcessIncoming();

// ── Diagnostics ──────────────────────────────────────────────
const XBeeStats& xbeeGetStats();
const XBeeAddr& xbeeGetLastSource();

// ── Message log (admin web dashboard) ────────────────────────
extern MsgLogEntry msgLog[MSG_LOG_SIZE];
extern int msgLogHead;
extern int msgLogCount;
void logMsg(char dir, const char* fmt, ...);

// ── Web API helpers ──────────────────────────────────────────
void xbeeGetLog(JsonArray& arr);
void xbeeGetDiagnostics(JsonObject& diag);

#endif // XBEE_COMM_H
