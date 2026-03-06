# Notes for HopFog-Node Copilot Agent

> **Context:** These notes are for the Copilot agent working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node) branch `copilot/add-xbee-node-functionality`. The admin repo ([hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)) has been updated with full node protocol support and comprehensive XBee diagnostics.

---

## Current Status — BOTH SIDES ARE CODE-COMPATIBLE

The admin and node use **identical XBee code**:

| Feature | Admin (HopFog-Web) | Node (HopFog-Node) |
|---------|-------------------|-------------------|
| XBee mode | API mode 1 (AP=1) ✅ | API mode 1 (AP=1) ✅ |
| TX frame | 0x10 Transmit Request ✅ | 0x10 Transmit Request ✅ |
| RX frame | 0x90 state machine ✅ | 0x90 state machine ✅ |
| ESP32-CAM TX | GPIO 13 → XBee DIN ✅ | GPIO 13 → XBee DIN ✅ |
| ESP32-CAM RX | GPIO 12 ← XBee DOUT ✅ | GPIO 12 ← XBee DOUT ✅ |
| Baud rate | 9600 ✅ | 9600 ✅ |
| JSON protocol | REGISTER/HEARTBEAT/SYNC ✅ | REGISTER/HEARTBEAT/SYNC ✅ |

**The code is fully aligned.** Any communication failure is a hardware/configuration issue.

---

## ⚠️ KNOWN ISSUE: "No bytes received from XBee serial"

The admin reports 0 bytes received even though the XBees work in XCTU. The admin has a **comprehensive diagnostic system** on the testing page to help debug this.

### Possible Causes and Fixes

#### 1. XBee Module Not in API Mode 1

**The most common cause.** The XBee must be configured in XCTU with `AP=1` (API mode without escapes). If it's in AT mode (AP=0), the XBee outputs raw text, not binary API frames, and vice versa.

**How to verify:**
1. Connect the XBee module to XCTU via USB adapter
2. Read the configuration
3. Check that `AP` = 1 (NOT 0 or 2)
4. Set `CE` = 1 (Coordinator) for admin, `CE` = 0 (Router) for node
5. Set `ID` to the same PAN ID on both (e.g., `1234`)
6. Set `BD` = 3 (9600 baud)
7. Write the configuration and close XCTU before connecting to ESP32

**CRITICAL: After writing XCTU settings, power-cycle the XBee before connecting it to the ESP32.**

#### 2. XBee Not Associated with Network

A Router XBee (CE=0) must discover and join the Coordinator's network before it can send/receive RF data. The XBee module's ASSOC LED should be blinking (searching) or solid (associated).

**How to verify:**
1. Open XCTU with the Router XBee connected
2. Check the `AI` (Association Indication) parameter:
   - `0x00` = Successfully joined ✅
   - `0xFF` = Scanning for network ⏳
   - `0x21` = No coordinator found ❌
   - `0x22` = PAN ID mismatch ❌
3. Make sure the Coordinator XBee is powered on and configured with the same PAN ID

#### 3. GPIO 12 Boot Strapping Issue (ESP32-CAM)

GPIO 12 is a strapping pin that determines flash voltage at boot. If the XBee's DOUT pulls GPIO 12 HIGH at power-on, the ESP32 will try to boot with 1.8V flash and crash.

**Symptoms:** ESP32-CAM boot loops, never reaches setup()

**Fix options:**
1. **Disconnect XBee DOUT during power-on** — reconnect after boot
2. **Add a 10kΩ pull-down resistor** from GPIO 12 to GND
3. **Burn the VDD_SDIO efuse** (permanent): `espefuse.py set_flash_voltage 3.3V`

#### 4. SD_MMC Stealing GPIO 12/13

On ESP32-CAM, the SD card uses SD_MMC mode. Even in 1-bit mode (which should only use GPIO 2, 14, 15), the ESP-IDF driver may configure GPIO 12/13 as SDMMC pins internally.

**The admin already has a fix for this:** After `Serial2.begin()`, it explicitly calls `uart_set_pin()` to reclaim GPIO 12/13 for UART2. The node should do the same:

```cpp
#include <driver/uart.h>

// In setup(), after xbeeSerial.begin():
xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
uart_set_pin(UART_NUM_2, XBEE_TX_PIN, XBEE_RX_PIN,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

**Add this to the node's `setup()` function right after the `xbeeSerial.begin()` call.**

#### 5. Wiring Check

```
ESP32-CAM               XBee Module
=========               ===========
GPIO 13  (TX) ────────► DIN   (pin 3)
GPIO 12  (RX) ◄──────── DOUT  (pin 2)
GND           ────────── GND   (pin 10)
3.3V          ────────── VCC   (pin 1)
```

**Important:** XBee VCC must be 3.3V, NOT 5V!

---

## Admin Diagnostic Tools

The admin testing page (`/admin/messaging/testing`) has a comprehensive XBee serial monitor with:

### Diagnostics Panel
- **TX Direction:** Shows ✅/❌ with byte count and TX status acknowledgments
- **RX Direction:** Shows ✅/❌ with byte count and parsed frame count
- **Assessment:** Human-readable connection status message
- **GPIO States:** Real-time readback of GPIO 12/13 logic levels
- **Raw Hex Dump:** First 64 raw bytes received (for identifying AT vs API mode issues)

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /api/xbee/status` | GET | Pin config, baud rate, mode |
| `GET /api/xbee/rx-log` | GET | Last 50 events + diagnostic counters |
| `GET /api/xbee/diagnostics` | GET | Full hardware diagnostic (GPIO states, raw bytes, assessment) |
| `POST /api/xbee/test` | POST | Send test message (HOPFOG_TEST\|...) |
| `POST /api/xbee/send-raw` | POST | Send arbitrary text via XBee API frame |
| `POST /api/xbee/loopback-test` | POST | UART self-test (requires TX↔RX jumper) |

### Using the Diagnostics

1. Open the admin testing page → XBee Serial Monitor
2. Click "Run Diagnostics" button
3. Check the diagnostic output:

   | Scenario | TX Status | RX Status | Meaning |
   |----------|-----------|-----------|---------|
   | ✅ TX, ✅ RX | ACKs received | Frames parsed | **Working!** |
   | ✅ TX, ❌ RX (0 bytes) | ACKs received | No data | Node not sending, or node TX→admin RX wiring issue |
   | ✅ TX, ⚠️ RX (bytes but no frames) | ACKs received | Raw bytes but unparseable | XBee AP mode mismatch (one in AT, one in API) |
   | ⚠️ TX (no ACK), ❌ RX | Sent but no ACK | No data | XBee not in AP=1, or XBee not powered |
   | ❌ TX, ❌ RX | 0 bytes | 0 bytes | UART not functioning — check wiring |

4. If **TX shows ACKs but RX shows 0 bytes**:
   - The admin ESP32 → XBee serial works (admin can send TO XBee)
   - But XBee DOUT → admin ESP32 doesn't work (can't receive FROM XBee)
   - This suggests GPIO 12 is not properly configured as UART2 RX
   - The `uart_set_pin()` fix should resolve this

---

## Action Items for the Node Agent

### Required Change: Add uart_set_pin() Fix

In `src/main.cpp`, in the `setup()` function, add the explicit pin reclaim after `xbeeSerial.begin()`:

```cpp
// At the top of main.cpp, add:
#include <driver/uart.h>

// In setup(), after xbeeSerial.begin():
#ifdef ARDUINO_ARCH_ESP32
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    // Explicitly reclaim GPIO pins for UART2 — this overrides any earlier
    // pin matrix configuration that SD_MMC.begin() may have set
    uart_set_pin(UART_NUM_2, XBEE_TX_PIN, XBEE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#elif defined(ARDUINO_ARCH_ESP8266)
    xbeeSerial.begin(XBEE_BAUD);
#endif
```

### Optional: Add Diagnostic Byte Counter

Add a `totalRxBytes` counter to the node's `xbeeProcessIncoming()` for debugging:

```cpp
static unsigned long nodeRxBytes = 0;

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        nodeRxBytes++;
        // ... rest of state machine
    }
}
```

Then expose it in the `/api/stats` response so it can be checked remotely.

---

## XBee Module Configuration (XCTU)

| Parameter | Admin XBee (Coordinator) | Node XBee (Router) |
|-----------|--------------------------|---------------------|
| **AP** | `1` (API mode 1) | `1` (API mode 1) |
| **CE** | `1` (Coordinator) | `0` (Router) |
| **ID** | `1234` (PAN ID) | `1234` (same PAN ID) |
| **BD** | `3` (9600 baud) | `3` (9600 baud) |
| **JV** | _(any)_ | `1` (Join verification) |

> **After writing XCTU settings, power-cycle the XBee before connecting to ESP32!**

---

## Protocol Reference

The JSON command protocol is unchanged:

```json
{"cmd":"COMMAND_NAME","node_id":"node-01","ts":12345,"params":{...}}
```

### Command Flow

```
Node boots up
    │
    ├─► REGISTER ──────────────► Admin (every 10s until ACK)
    │                               │
    │   ◄──── REGISTER_ACK ◄───────┘
    │
    ├─► SYNC_REQUEST ──────────► Admin
    │                               │
    │   ◄──── SYNC_DATA ◄──────────┘
    │         (users, announcements, conversations, chat_messages, fog_nodes)
    │
    └─► HEARTBEAT (every 30s) ─► Admin
                                    │
        ◄──── PONG ◄────────────────┘
```
