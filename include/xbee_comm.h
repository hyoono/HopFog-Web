/*
 * xbee_comm.h — Digi XBee S2C (ZigBee) serial interface.
 *
 * Uses AT/transparent mode (AP=0) with newline-delimited JSON.
 * This is compatible with HopFog-Node which also uses AT mode.
 *
 * XBee modules must be configured:
 *   AP  = 0 (transparent mode)
 *   DH  = 0, DL = FFFF (broadcast)
 *   BD  = 3 (9600 baud)
 *   ID  = same PAN ID on all modules
 *
 * Both ESP32-CAM and generic ESP32 use UART2 (Serial2) on GPIO 13/12
 * so that UART0 (Serial) stays free for Serial Monitor debug output.
 *
 * Wiring (same for all ESP32 boards):
 *   ESP32 GPIO 13 (TX) → XBee DIN   (pin 3)
 *   ESP32 GPIO 12 (RX) ← XBee DOUT  (pin 2)
 *   ESP32 3.3 V         → XBee VCC   (pin 1)
 *   ESP32 GND            → XBee GND   (pin 10)
 */

#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>

// ── Public API ──────────────────────────────────────────────────────

/// Initialise the XBee serial port (UART2, 9600 baud).
void xbeeInit();

/// Send a text payload as a newline-terminated line.
/// In AT mode the XBee broadcasts to all nodes (DH=0, DL=FFFF).
/// Returns 1 on success, 0 if payload is empty or too long.
uint8_t xbeeSendBroadcast(const char* payload, size_t len);

/// Call from loop() to process any incoming XBee data.
/// Buffers bytes until a newline ('\n') is received, then passes
/// the complete line to the callback set by xbeeSetReceiveCallback().
void xbeeProcessIncoming();

/// Callback signature: (line text, length)
/// In AT mode there is no sender address — all data is broadcast.
typedef void (*XBeeReceiveCB)(const char* line, size_t len);

/// Register a callback for incoming lines.
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

#endif // XBEE_COMM_H
