/*
 * xbee_comm.h — Digi XBee S2C (ZigBee) serial interface.
 *
 * Uses API mode 1 (AP=1) to exchange framed messages with a remote
 * XBee S2C coordinator/end-device.
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

// ── XBee API frame types ────────────────────────────────────────────
#define XBEE_START_DELIM  0x7E
#define XBEE_TX_REQUEST   0x10  // ZigBee Transmit Request
#define XBEE_TX_STATUS    0x8B  // ZigBee Transmit Status
#define XBEE_RX_PACKET    0x90  // ZigBee Receive Packet

// ── Broadcast address (send to all nodes in the PAN) ────────────────
#define XBEE_BROADCAST_ADDR64_HI  0x00000000
#define XBEE_BROADCAST_ADDR64_LO  0x0000FFFF
#define XBEE_BROADCAST_ADDR16     0xFFFE

// ── Public API ──────────────────────────────────────────────────────

/// Initialise the XBee serial port.
/// ESP32-CAM: reconfigures UART0 (Serial) to 9600 baud for XBee.
/// Generic ESP32: starts UART2 (Serial2) on GPIO 13/12.
void xbeeInit();

/// Send a text payload to the ZigBee broadcast address.
/// Returns the frame ID (1-255) used, or 0 on failure.
uint8_t xbeeSendBroadcast(const char* payload, size_t len);

/// Send a text payload to a specific 64-bit XBee address.
uint8_t xbeeSendUnicast(uint32_t addrHi, uint32_t addrLo,
                        const char* payload, size_t len);

/// Call from loop() to process any incoming XBee frames.
/// Received text payloads are passed to the callback set by
/// xbeeSetReceiveCallback().
void xbeeProcessIncoming();

/// Callback signature: (payload, length, senderAddrHi, senderAddrLo)
typedef void (*XBeeReceiveCB)(const uint8_t* data, size_t len,
                              uint32_t senderHi, uint32_t senderLo);

/// Register a callback for incoming data frames.
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

#endif // XBEE_COMM_H
