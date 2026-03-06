# Notes for HopFog-Node Copilot Agent

> **Context:** These notes are for the Copilot agent working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node) branch `copilot/add-xbee-node-functionality`. The admin repo ([hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)) has been updated with full node protocol support and an XBee serial monitor for debugging.

---

## Current Status

**Both admin and node already use XBee API mode 1** — the binary frame format is identical. The JSON command protocol is also identical. Communication should work.

### What Works

| Feature | Admin (HopFog-Web) | Node (HopFog-Node) |
|---------|-------------------|-------------------|
| XBee mode | API mode 1 (AP=1) ✅ | API mode 1 (AP=1) ✅ |
| TX frame | 0x10 Transmit Request ✅ | 0x10 Transmit Request ✅ |
| RX frame | 0x90 state machine ✅ | 0x90 state machine ✅ |
| JSON protocol | REGISTER/HEARTBEAT/SYNC/etc ✅ | REGISTER/HEARTBEAT/SYNC/etc ✅ |
| Baud rate | 9600 ✅ | 9600 ✅ |

### ⚠️ Known Issue: Pin Assignment Difference

The admin and node use **different GPIO pin assignments** for the same ESP32-CAM board:

| | Admin (HopFog-Web) | Node (HopFog-Node) |
|---|---|---|
| **ESP32 TX → XBee DIN** | GPIO **13** | GPIO **12** |
| **ESP32 RX ← XBee DOUT** | GPIO **12** | GPIO **13** |
| `Serial2.begin()` call | `begin(9600, 8N1, RX=12, TX=13)` | `begin(9600, 8N1, RX=13, TX=12)` |

**Impact:** If both ESP32-CAMs are wired the same way (e.g., GPIO 13 → XBee DIN, GPIO 12 → XBee DOUT), only one of them will work. The other will have TX and RX swapped.

**Recommendation:** Either:
1. **Change the node to match the admin convention** (GPIO 13=TX, GPIO 12=RX) — preferred
2. **Or wire the node's ESP32-CAM differently** — GPIO 12 → XBee DIN, GPIO 13 → XBee DOUT

### To change the node pin convention to match admin:

In `main.cpp`, change the ESP32-CAM defaults:

```cpp
// CURRENT (different from admin):
#ifndef XBEE_RX_PIN
  #define XBEE_RX_PIN 13
#endif
#ifndef XBEE_TX_PIN
  #define XBEE_TX_PIN 12
#endif

// CHANGE TO (match admin convention):
#ifndef XBEE_RX_PIN
  #define XBEE_RX_PIN 12    // ESP32 GPIO 12 ← XBee DOUT (pin 2)
#endif
#ifndef XBEE_TX_PIN
  #define XBEE_TX_PIN 13    // ESP32 GPIO 13 → XBee DIN  (pin 3)
#endif
```

Then wire the node's ESP32-CAM identically to the admin:
```
ESP32 GPIO 13 ──→ XBee DIN   (pin 3)
ESP32 GPIO 12 ←── XBee DOUT  (pin 2)
ESP32 3.3V    ──→ XBee VCC   (pin 1)
ESP32 GND     ──→ XBee GND   (pin 10)
```

---

## XBee Module Configuration (XCTU)

Both XBee modules must be configured in XCTU:

| Parameter | Admin XBee (Coordinator) | Node XBee (Router) |
|-----------|--------------------------|---------------------|
| **AP** | `1` (API mode 1) | `1` (API mode 1) |
| **CE** | `1` (Coordinator) | `0` (Router) |
| **ID** | `1234` (PAN ID) | `1234` (same PAN ID) |
| **BD** | `3` (9600 baud) | `3` (9600 baud) |
| **JV** | _(any)_ | `1` (Join verification — helps router find coordinator) |

> **DH/DL are not needed in API mode** — the destination address is in each TX Request frame header.

---

## Debugging with the Admin Serial Monitor

The admin now has an **XBee Serial Monitor** on the Testing page (`/admin/messaging/testing`). It shows:

- **All transmitted frames** (TX →) with payload preview
- **All received frames** (RX ←) with source address and JSON payload
- **TX delivery status** (0x8B) — success or failure
- **Errors** — checksum failures, invalid frame lengths
- **Raw byte counter** — if 0, the ESP32 isn't reading any serial data from XBee (wiring issue)

### API Endpoints for Debugging

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /api/xbee/status` | GET | Pin config, baud rate, mode |
| `GET /api/xbee/rx-log` | GET | Last 50 events with timestamps, payload previews |
| `POST /api/xbee/test` | POST | Send a test message (`HOPFOG_TEST|...`) |
| `POST /api/xbee/send-raw` | POST | Send arbitrary text (e.g., a JSON command) |

### Troubleshooting with the Serial Monitor

1. **"0 bytes received"** → ESP32 is not reading any data from the XBee serial port
   - Check wiring (TX/RX may be swapped)
   - Check baud rate matches XCTU setting (BD=3 = 9600)
   - Check XBee is powered (3.3V, not 5V!)
   - Try swapping GPIO 12 and GPIO 13 connections

2. **"Checksum error"** → Frames are arriving but corrupted
   - Check for loose connections
   - Check baud rate match
   - Ensure XBee is in API mode 1 (AP=1), not API mode 2 (AP=2, escaped)

3. **TX frames show "delivery FAILED 0x24"** → No route to destination
   - XBee modules haven't formed a network
   - Check PAN ID matches (ID parameter)
   - Check Coordinator/Router assignment (CE parameter)

4. **TX frames show sent but no RX frames** → One direction works
   - The sending ESP32→XBee serial works, but the other ESP32←XBee doesn't
   - Most likely a TX/RX pin swap on one of the boards

---

## Protocol Reference

The JSON protocol is unchanged. All commands use this format:

```json
{"cmd":"COMMAND_NAME","node_id":"node-01","ts":12345,"params":{...}}
```

### Command Flow

```
Node boots up
    │
    ├─► REGISTER ────────────► Admin
    │                              │
    │   ◄──── REGISTER_ACK ◄──────┘
    │
    ├─► SYNC_REQUEST ────────► Admin
    │                              │
    │   ◄──── SYNC_DATA ◄─────────┘
    │         (users, announcements, conversations, chat_messages)
    │
    └─► HEARTBEAT (every 30s) ─► Admin
                                   │
        ◄──── PONG ◄───────────────┘
```

### Admin's SYNC_DATA Payload

The admin sends these collections (with size limits to stay within XBee payload constraints):

- `users` — all active users (id, username, email, role, is_active, has_agreed_sos)
- `announcements` — last 50 sent/queued broadcasts (id, title, message, created_at)
- `conversations` — all conversations
- `chat_messages` — last 100 direct messages
- `fog_nodes` — all registered fog devices
- `messages` — empty array (reserved)

---

## Checklist

1. [ ] **Fix pin assignment** — change `XBEE_RX_PIN` from 13 to 12 and `XBEE_TX_PIN` from 12 to 13 (match admin convention)
2. [ ] **Verify XBee XCTU config** — AP=1 (API mode), CE=0 (Router), ID=1234, BD=3 (9600)
3. [ ] **Wire ESP32-CAM** — GPIO 13 → XBee DIN (pin 3), GPIO 12 ← XBee DOUT (pin 2)
4. [ ] **Test** — send a REGISTER command, check admin serial monitor for receipt and REGISTER_ACK response
5. [ ] **Test** — verify HEARTBEAT → PONG round-trip
6. [ ] **Test** — verify SYNC_REQUEST → SYNC_DATA with user/announcement data
