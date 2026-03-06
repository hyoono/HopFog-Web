/*
 * xbee_comm.h — Digi XBee S2C (ZigBee) serial interface.
 *
 * Uses XBee API mode 1 (AP=1) with binary-framed packets.
 * JSON payloads are wrapped in 0x10 Transmit Request frames for
 * sending, and extracted from 0x90 Receive Packet frames on receive.
 *
 * XBee modules must be configured:
 *   AP  = 1 (API mode without escapes)
 *   CE  = 1 (Coordinator) on admin, CE = 0 (Router) on nodes
 *   BD  = 3 (9600 baud)
 *   ID  = same PAN ID on all modules
 *
 * Uses UART2 (Serial2) so that UART0 (Serial) stays free for
 * Serial Monitor debug output.
 *
 * ESP32-CAM wiring (SPI SD mode — GPIO 4 and 12 are free):
 *   ESP32 GPIO  4 (TX) → XBee DIN   (pin 3)
 *   ESP32 GPIO 12 (RX) ← XBee DOUT  (pin 2)
 *   ESP32 3.3 V         → XBee VCC   (pin 1)
 *   ESP32 GND            → XBee GND   (pin 10)
 *
 * Generic ESP32 wiring:
 *   ESP32 GPIO 13 (TX) → XBee DIN   (pin 3)
 *   ESP32 GPIO 12 (RX) ← XBee DOUT  (pin 2)
 */

#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── API Mode 1 Constants ────────────────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10  // Transmit Request frame type
#define XBEE_RX_PACKET     0x90  // Receive Packet frame type
#define XBEE_TX_STATUS     0x8B  // Transmit Status frame type

// ── Event log ───────────────────────────────────────────────────────
#define XBEE_LOG_SIZE      50    // ring buffer capacity
#define XBEE_LOG_MSG_MAX  200    // max message text per entry

struct XBeeLogEntry {
    unsigned long ts;         // millis()
    char direction;           // 'T' = TX, 'R' = RX, 'E' = error, 'S' = status
    uint8_t frameType;        // 0x10, 0x90, 0x8B, or 0 for errors
    uint8_t frameId;          // frame ID (if applicable)
    char msg[XBEE_LOG_MSG_MAX]; // human-readable summary / payload text
};

// ── Public API ──────────────────────────────────────────────────────

/// Initialise the XBee serial port (UART2, 9600 baud, API mode 1).
void xbeeInit();

/// Build a 0x10 Transmit Request frame and send the payload as a
/// broadcast (64-bit dest = 0x000000000000FFFF).
/// Returns the frame ID (1-255) on success, 0 on failure.
uint8_t xbeeSendBroadcast(const char* payload, size_t len);

/// Call from loop() to process any incoming XBee serial data.
/// Reassembles API frames byte-by-byte and, for 0x90 Receive Packet
/// frames, extracts the RF data payload and passes it to the callback.
void xbeeProcessIncoming();

/// Callback signature: (payload text, length).
/// The payload is the raw RF data extracted from the 0x90 frame
/// (typically a JSON line without trailing newline).
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

/// Register a callback for incoming receive packets.
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

/// Populate a JSON array with the event log (newest first).
void xbeeGetLog(JsonArray& arr);

/// Return the total count of raw bytes received on the serial port
/// (useful for diagnostics — if 0, the ESP32 isn't reading from XBee at all).
unsigned long xbeeGetRxByteCount();

/// Populate a JsonObject with comprehensive diagnostics:
/// TX/RX byte counts, frame counts, GPIO states, raw hex dump,
/// and a human-readable connection assessment.
void xbeeGetDiagnostics(JsonObject& diag);

/// Run a UART loopback test: writes 4 test bytes and tries to read them
/// back. Requires TX pin to be physically jumpered to RX pin.
/// Returns true if all 4 bytes were read back correctly.
/// Stores a human-readable result message in `result`.
bool xbeeRunLoopbackTest(String& result);

#endif // XBEE_COMM_H
