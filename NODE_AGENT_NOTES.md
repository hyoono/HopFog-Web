# Notes for HopFog-Node Copilot Agent

> **Context:** These notes are for the Copilot agent working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node) branch `copilot/add-xbee-node-functionality`. The admin repo ([hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)) has been updated with full node protocol support and comprehensive XBee diagnostics.

---

## Current Status — RX IS WORKING

The user has confirmed:
- ✅ **TX works** — admin can send test messages to XCTU and to the XBee network
- ✅ **RX works** — admin receives data back on GPIO 12 (frame parsing succeeds)
- ✅ **API mode 1** — both sides use identical 0x10/0x90 binary frames
- ✅ **Pin assignments match** — GPIO 13=TX→DIN, GPIO 12=RX←DOUT on both sides
- ⚠️ **Self-echo observed** — admin sees its own 0x10 TX frames echoed back as RX (frame type 0x10 in log). This is now handled gracefully (logged as "Self-echo ignored").
- ❓ **Node data not yet received** — admin has not yet received 0x90 RX Packet frames from the node

### What "Self-echo" Means

When the admin sends a test message, the serial monitor shows:
```
TX -> [0x10] (44 B) {"cmd":"BROADCAST_MSG",...}
RX <- [0x10] Self-echo ignored (44 B) — TX/RX may be bridged
```

This means the admin's own transmitted API frame is feeding back into its RX. This happens due to breadboard proximity, internal GPIO routing, or XBee echo. It does NOT prevent real communication — when the node sends an RF packet, the admin's XBee will output a 0x90 frame on DOUT, which will be parsed correctly.

---

## Code Alignment — Both Sides Match

| Feature | Admin (HopFog-Web) | Node (HopFog-Node) |
|---------|-------------------|-------------------|
| XBee mode | API mode 1 (AP=1) ✅ | API mode 1 (AP=1) ✅ |
| TX frame | 0x10 Transmit Request ✅ | 0x10 Transmit Request ✅ |
| RX frame | 0x90 state machine ✅ | 0x90 state machine ✅ |
| ESP32-CAM TX | GPIO 13 → XBee DIN ✅ | GPIO 13 → XBee DIN ✅ |
| ESP32-CAM RX | GPIO 12 ← XBee DOUT ✅ | GPIO 12 ← XBee DOUT ✅ |
| Baud rate | 9600 ✅ | 9600 ✅ |
| JSON protocol | REGISTER/HEARTBEAT/SYNC ✅ | REGISTER/HEARTBEAT/SYNC ✅ |

---

## ⚠️ REQUIRED: Add gpio_reset_pin() Fix to the Node

The admin now calls `gpio_reset_pin()` before `Serial2.begin()` to detach GPIO 12/13 from the SD_MMC IOMUX. **The node must do the same.** Without this, UART2 RX may not function on ESP32-CAM.

### What to Add

In the node's XBee initialization (wherever `Serial2.begin()` or `xbeeSerial.begin()` is called), add this **before** the begin call:

```cpp
#include <driver/gpio.h>
#include <driver/uart.h>

// BEFORE Serial2.begin():
// On ESP32-CAM, SD_MMC.begin() configures GPIO 12/13 via IOMUX as
// HS2_DATA2/DATA3, even in 1-bit mode.  IOMUX takes priority over
// the GPIO matrix that UART2 uses.  gpio_reset_pin() switches the
// pin back to GPIO function so UART2 can claim it.
gpio_reset_pin(GPIO_NUM_12);   // detach from HS2_DATA2 IOMUX
gpio_reset_pin(GPIO_NUM_13);   // detach from HS2_DATA3 IOMUX
Serial.println("[XBee] Reset GPIO 12/13 from SD_MMC IOMUX to GPIO function");

xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);

// Belt-and-suspenders: explicitly route UART2 signals to these pins
uart_set_pin(UART_NUM_2, XBEE_TX_PIN, XBEE_RX_PIN,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

**Even if the node does NOT use SD_MMC**, the `gpio_reset_pin()` calls are harmless and ensure clean GPIO state.

---

## UART Loopback Test — Expected Behavior

The admin has a UART loopback test button. **It is expected to show "No data read back" when the XBee is connected.** This is NOT a failure — the test sends raw non-API bytes (0xAA, 0x55...) which the XBee discards as invalid. To actually test UART hardware, you must disconnect the XBee and bridge GPIO 13→12 with a jumper wire.

**The real proof that RX works is seeing any frame in the serial monitor log** (even self-echo 0x10 frames). The user has confirmed this works.

---

## Debugging: Why the Node Doesn't Sync

If the admin's serial monitor shows self-echo but NO 0x90 frames from the node, check these on the **node side**:

### 1. Is the node actually sending?

Add a Serial.println debug line in the node's `xbeeSendBroadcast()` function:
```cpp
Serial.printf("[XBee-Node] TX: sending %d bytes via API frame\n", (int)len);
```

If this doesn't print, the node's timer/logic for sending REGISTER/HEARTBEAT isn't triggering.

### 2. Is the node's XBee associated?

The Router XBee must join the Coordinator's network before RF packets can be exchanged. Check:
- ASSOC LED on the XBee module (blinking = searching, solid = associated)
- In XCTU, read `AI` (Association Indication) — must be `0x00` (joined)
- Ensure both XBees have the **same PAN ID** (`ID` parameter)

### 3. Is the node using the right frame format?

The node's TX frame must be:
```
Byte:  7E  [LenHi] [LenLo]  10  [FID]  00 00 00 00 00 00 FF FF  FF FE  00 00  [payload...]  [checksum]
       ^^  ^^^^^^^^^^^^^^^^  ^^  ^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^  ^^^^^  ^^^^^^^^^^^^  ^^^^^^^^^^
       Start   Length       Type  ID    64-bit dest (broadcast)   16-bit  Opts   JSON data    0xFF-sum
```

The admin's state machine looks for `0x7E` as the start delimiter, then reads the 2-byte length, then the frame data, then validates the checksum (`sum of all frame bytes + checksum byte == 0xFF`).

### 4. Timing

The node should send REGISTER every 10 seconds until it receives REGISTER_ACK, then HEARTBEAT every 30 seconds. Make sure these timers are working.

---

## Admin Diagnostic Tools

The admin testing page (`/admin/messaging/testing`) has:

### Serial Monitor Console
- Color-coded entries (TX=yellow, RX=green, SYS=cyan, ERR=red)
- Auto-refresh every 2 seconds
- "Send Raw" text input for testing

### Diagnostics Panel
- **TX Direction:** ✅/❌ with byte count and TX status ACKs
- **RX Direction:** ✅/❌ with byte count, parsed frames, and self-echo count
- **Assessment:** Context-aware message based on what's working
- **Details:** GPIO states, raw hex dump, all counters

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /api/xbee/status` | GET | Pin config, baud rate |
| `GET /api/xbee/rx-log` | GET | Last 50 events + counters |
| `GET /api/xbee/diagnostics` | GET | Full diagnostic (GPIO, hex dump, assessment) |
| `POST /api/xbee/test` | POST | Send test broadcast message |
| `POST /api/xbee/send-raw` | POST | Send arbitrary text via XBee |
| `POST /api/xbee/loopback-test` | POST | UART self-test (requires jumper wire, XBee disconnected) |

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

The JSON command protocol (unchanged):

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
