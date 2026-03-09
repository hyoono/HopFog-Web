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

// ── Message log ──────────────────────────────────────────────
#define MSG_LOG_SIZE  60

struct MsgLogEntry {
    unsigned long ts;
    char dir;               // 'T'=TX, 'R'=RX, 'S'=status, 'E'=error
    char text[200];
};

// Callback for received data (0x90 frame payload)
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
inline uint8_t xbeeSendBroadcast(const char* text) {
    return xbeeSendBroadcast(text, strlen(text));
}
void xbeeSetReceiveCallback(XBeeReceiveCB cb);
void xbeeProcessIncoming();

// ── Diagnostics ──────────────────────────────────────────────
extern unsigned long xbTotalRxBytes;
extern unsigned long xbTotalTxBytes;
extern unsigned long xbTxStatusOk;
extern unsigned long xbTxStatusFail;
extern unsigned long xbRxFramesParsed;
extern unsigned long xbSelfEchoCount;

extern MsgLogEntry msgLog[MSG_LOG_SIZE];
extern int msgLogHead;
extern int msgLogCount;
void logMsg(char dir, const char* fmt, ...);

// ── Web API helpers ──────────────────────────────────────────
void xbeeGetLog(JsonArray& arr);
void xbeeGetDiagnostics(JsonObject& diag);

#endif // XBEE_COMM_H
