# HopFog-Node — Build From Scratch Instructions

> **For:** Copilot agent (Claude Opus 4.6) working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
>
> These instructions are split into 3 parts (each under 30K characters) so they fit within the Copilot agent's input limit.

---

## How to Use These Notes

Give each part to the Copilot agent as a separate task, **in order**:

### Part 1: Setup & XBee Driver (~18K chars)
**File:** [`NODE_AGENT_NOTES_PART1.md`](NODE_AGENT_NOTES_PART1.md)

**Covers:**
- Architecture overview (admin ↔ node diagram)
- Hardware wiring (ESP32-CAM ↔ XBee S2C pinout)
- XCTU configuration (AP=1, CE=0, ID=1234, BD=3, JV=1)
- PlatformIO project setup (platformio.ini)
- File structure
- `config.h` — complete code
- `xbee_comm.h` — complete code
- `xbee_comm.cpp` — complete XBee API mode 1 driver code (0x10 TX, 0x90 RX state machine, gpio_reset_pin fix)

**Copilot task prompt:**
> "Read the instructions in NODE_AGENT_NOTES_PART1.md from the HopFog-Web repo. Create all the files described: platformio.ini, config.h, xbee_comm.h, xbee_comm.cpp. Use the exact code provided. Make sure to include the gpio_reset_pin() fix."

---

### Part 2: Node Client & Storage (~11K chars)
**File:** [`NODE_AGENT_NOTES_PART2.md`](NODE_AGENT_NOTES_PART2.md)

**Covers:**
- `node_client.h` — state machine (UNREGISTERED → REGISTERED → SYNCING → RUNNING)
- `node_client.cpp` — complete code: REGISTER/HEARTBEAT/SYNC_REQUEST senders, REGISTER_ACK/PONG/SYNC_DATA/BROADCAST_MSG/GET_STATS handlers
- `sd_storage.h` — read/write JSON files
- `sd_storage.cpp` — SD_MMC 1-bit mode init, JSON file I/O

**Copilot task prompt:**
> "Read the instructions in NODE_AGENT_NOTES_PART2.md from the HopFog-Web repo. Create: node_client.h, node_client.cpp, sd_storage.h, sd_storage.cpp. Use the exact code provided."

---

### Part 3: Web Server, Protocol & Debugging (~15K chars)
**File:** [`NODE_AGENT_NOTES_PART3.md`](NODE_AGENT_NOTES_PART3.md)

**Covers:**
- Mobile app API endpoints (12 endpoints to implement)
- How to relay user actions to admin via XBee (code examples)
- `main.cpp` — complete setup() and loop()
- Protocol reference (binary frame diagrams, JSON command tables, flow diagram)
- **CRITICAL: GPIO fix** for ESP32-CAM (most important debugging info)
- Debugging guide (5-step sequence)
- Implementation checklist (3 phases with checkboxes)
- Admin diagnostic endpoints (for remote debugging)

**Copilot task prompt:**
> "Read the instructions in NODE_AGENT_NOTES_PART3.md from the HopFog-Web repo. Create main.cpp and the web server / API handler files. Implement the mobile app API endpoints. Follow the implementation checklist."

---

## Quick Reference

| Item | Value |
|------|-------|
| XBee mode | API mode 1 (AP=1) — binary frames |
| Admin XBee | Coordinator (CE=1) |
| Node XBee | Router (CE=0, JV=1) |
| PAN ID | 1234 (must match) |
| Baud | 9600 |
| GPIO TX | 13 → XBee DIN (pin 3) |
| GPIO RX | 12 ← XBee DOUT (pin 2) |
| Frame format | 0x7E | Length(2B) | FrameData | Checksum |
| TX frame type | 0x10 (Transmit Request) |
| RX frame type | 0x90 (Receive Packet) |
| Status frame | 0x8B (Transmit Status) |
| JSON format | `{"cmd":"NAME","node_id":"node-01","ts":123,"params":{...}}` |

## Critical Reminders

1. **`gpio_reset_pin(GPIO_NUM_12)` and `gpio_reset_pin(GPIO_NUM_13)` MUST be called before `Serial2.begin()`** — without this, XBee RX will not work on ESP32-CAM
2. **SD card init MUST come before XBee init** — `initSDCard()` then `xbeeInit()` in that order
3. **Both XBee modules must be in API mode 1 (AP=1)** — configure via XCTU before connecting to ESP32
