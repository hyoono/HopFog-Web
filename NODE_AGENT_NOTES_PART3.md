# HopFog-Node — Build From Scratch (Part 3 of 3: Web Server, Protocol & Debugging)

> **For:** Copilot agent (Claude Opus 4.6) working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
>
> **Prerequisites:** Complete Parts 1 and 2 first (XBee driver, node client, SD storage).
>
> **This part covers:** Web server for mobile app, main.cpp, protocol reference, GPIO fix, debugging, and implementation checklist.

---

## 12. Implementation: Web Server & API Handlers

The node should run its own WiFi AP and serve the same mobile app API endpoints as the admin. Mobile phones connect to the node's WiFi and use these endpoints.

### Minimum Required Endpoints

These are the endpoints the HopFogMobile app calls:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /status` | GET | Health check — `{"online": true}` |
| `POST /login` | POST | Login — JSON `{username, password}` → user object |
| `GET /announcements` | GET | List announcements (from synced data) |
| `GET /users` | GET | List users (from synced data) |
| `GET /conversations` | GET | List conversations for a user |
| `GET /messages` | GET | List messages in a conversation |
| `POST /send` | POST | Send a message (store locally + relay to admin) |
| `POST /create-chat` | POST | Create a conversation (store locally + relay) |
| `POST /sos` | POST | Trigger SOS alert (relay to admin immediately) |
| `GET /new-messages` | GET | Poll for new messages |
| `POST /agree-sos` | POST | Record SOS agreement |
| `POST /change-password` | POST | Change password (relay to admin) |

### How to Relay User Actions to Admin

When a mobile user performs an action (send message, SOS, etc.) on the node:
1. Store the data locally on the node's SD card
2. Send a relay command to admin via XBee:

```cpp
// Example: relay a chat message to admin
void relayChatMessage(int convId, int senderId, const char* text) {
    JsonDocument doc;
    doc["cmd"] = "RELAY_CHAT_MSG";
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    JsonObject params = doc["params"].to<JsonObject>();
    params["conversation_id"] = convId;
    params["sender_id"] = senderId;
    params["message_text"] = text;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// Example: relay SOS alert to admin
void relaySosAlert(int userId) {
    JsonDocument doc;
    doc["cmd"] = "SOS_ALERT";
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    JsonObject params = doc["params"].to<JsonObject>();
    params["user_id"] = userId;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// Example: relay password change to admin
void relayChangePassword(int userId, const char* oldPw, const char* newPw) {
    JsonDocument doc;
    doc["cmd"] = "CHANGE_PASSWORD";
    doc["node_id"] = NODE_ID;
    JsonObject params = doc["params"].to<JsonObject>();
    params["user_id"] = userId;
    params["old_password"] = oldPw;
    params["new_password"] = newPw;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}
```

---

## 13. Implementation: main.cpp

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"

// If you add ESPAsyncWebServer for mobile API:
// #include <ESPAsyncWebServer.h>
// AsyncWebServer server(HTTP_PORT);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("   HopFog-Node  ESP32 Firmware");
    Serial.println("========================================");

    // 1. SD card
    if (!initSDCard()) {
        Serial.println("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 2. WiFi access point
    Serial.printf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power-saving for stability
    Serial.printf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 3. Web server with mobile API endpoints
    // setupWebServer(server);
    // registerApiHandlers(server);
    // server.begin();

    // 4. XBee + node protocol
    xbeeInit();
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            Serial.printf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    Serial.println("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
```

---

## 14. Protocol Reference

### Frame Structure (API Mode 1)

Every XBee serial exchange uses binary API frames:

```
┌──────┬──────────────┬───────────────────────────────────┬──────────┐
│ 0x7E │ Length (2B)   │ Frame Data (variable)             │ Checksum │
│ 1B   │ Hi Lo        │ [Type][...header...][payload]      │ 1B       │
└──────┴──────────────┴───────────────────────────────────┴──────────┘
```

**Checksum:** `0xFF - (sum of all frame data bytes)`

### 0x10 Transmit Request (Node → XBee → Air → Admin's XBee → 0x90)

```
Frame data (14 + N bytes):
  [0]     = 0x10  (frame type)
  [1]     = Frame ID (1-255)
  [2-9]   = 64-bit destination: 00 00 00 00 00 00 FF FF (broadcast)
  [10-11] = 16-bit destination: FF FE (broadcast)
  [12]    = Broadcast radius: 00 (max hops)
  [13]    = Options: 00
  [14..N] = RF data payload (your JSON string)
```

### 0x90 Receive Packet (Remote XBee → Air → Local XBee → ESP32)

```
Frame data (12 + N bytes):
  [0]     = 0x90  (frame type)
  [1-8]   = 64-bit source address
  [9-10]  = 16-bit source address
  [11]    = Receive options
  [12..N] = RF data payload (the JSON string)
```

### 0x8B Transmit Status (XBee → ESP32, confirms TX delivery)

```
Frame data (7 bytes):
  [0]     = 0x8B  (frame type)
  [1]     = Frame ID (matches your TX request)
  [2-3]   = 16-bit destination
  [4]     = Retry count
  [5]     = Delivery status: 0x00 = success, others = failure
  [6]     = Discovery status
```

### JSON Command Format

All JSON payloads follow this structure:
```json
{"cmd":"COMMAND_NAME","node_id":"node-01","ts":12345,"params":{...}}
```

### Node → Admin Commands

| Command | When | Params |
|---------|------|--------|
| `REGISTER` | Every 10s until ACK | `{device_name, ip_address, status, free_heap}` |
| `HEARTBEAT` | Every 30s after registered | `{ip_address, uptime, free_heap}` |
| `SYNC_REQUEST` | After REGISTER_ACK | _(none)_ |
| `RELAY_CHAT_MSG` | When mobile user sends a message | `{conversation_id, sender_id, message_text}` |
| `SOS_ALERT` | When mobile user triggers SOS | `{user_id}` |
| `CHANGE_PASSWORD` | When mobile user changes password | `{user_id, old_password, new_password}` |
| `RELAY_FOG_NODE` | When a fog device connects to node | `{device_name, ip_address, status}` |
| `STATS_RESPONSE` | In response to GET_STATS | `{free_heap, uptime, ip_address, wifi_stations}` |

### Admin → Node Commands

| Command | When |
|---------|------|
| `REGISTER_ACK` | In response to REGISTER |
| `PONG` | In response to HEARTBEAT |
| `SYNC_DATA` | In response to SYNC_REQUEST — contains `{users, announcements, conversations, chat_messages, fog_nodes}` |
| `BROADCAST_MSG` | When admin creates a broadcast |
| `GET_STATS` | When admin wants node stats |

### Command Flow Diagram

```
Node                                     Admin
  │                                        │
  ├──► REGISTER ──────────────────────────►│
  │    (every 10s)                         │
  │                                        │
  │◄──────────────────── REGISTER_ACK ◄────┤
  │                                        │
  ├──► SYNC_REQUEST ──────────────────────►│
  │                                        │
  │◄──────────────────── SYNC_DATA ◄───────┤
  │    (users, announcements, etc.)        │
  │                                        │
  ├──► HEARTBEAT ─────────────────────────►│
  │    (every 30s)                         │
  │◄──────────────────── PONG ◄────────────┤
  │                                        │
  │    [Mobile user sends message]         │
  ├──► RELAY_CHAT_MSG ────────────────────►│
  │                                        │
  │    [Admin creates broadcast]           │
  │◄──────────────────── BROADCAST_MSG ◄───┤
  │                                        │
```

---

## 15. CRITICAL: GPIO Fix for ESP32-CAM

**This is the #1 cause of "no communication" on ESP32-CAM boards.**

On ESP32-CAM, the SD card uses `SD_MMC.begin()` which configures GPIO 12 and 13 via the **IOMUX** peripheral as HS2_DATA2 and HS2_DATA3 (even in 1-bit mode where they're not needed). The UART2 peripheral routes through the **GPIO matrix**, which has lower priority than IOMUX.

**Result:** UART TX still works (output can override), but UART RX does NOT work (input is routed to the SD peripheral instead of UART2).

**Fix:** Call `gpio_reset_pin()` BEFORE `Serial2.begin()`:

```cpp
#include <driver/gpio.h>
#include <driver/uart.h>

// This MUST be called AFTER SD_MMC.begin() and BEFORE Serial2.begin()
gpio_reset_pin(GPIO_NUM_12);   // Detach from HS2_DATA2
gpio_reset_pin(GPIO_NUM_13);   // Detach from HS2_DATA3

Serial2.begin(9600, SERIAL_8N1, 12, 13);

uart_set_pin(UART_NUM_2, 13, 12,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

**The admin has confirmed this fix works.** Without it, the loopback test fails (write 4 bytes, read 0 back). With it, RX works correctly.

---

## 16. Debugging Guide

### Step 1: Verify XBee Hardware
1. Power on both admin and node
2. Check XBee ASSOC LED: blinking = searching, steady = joined
3. In XCTU: read `AI` parameter on node XBee → must be `0x00`

### Step 2: Verify ESP32 Serial
1. Open Serial Monitor on the node (115200 baud)
2. Look for: `[XBee] UART2 init: TX=GPIO13 RX=GPIO12 baud=9600 (API mode 1)`
3. Look for: `[Node] Sent REGISTER` (should appear every 10 seconds)
4. If no REGISTER prints, the node's main loop or timer isn't running

### Step 3: Verify TX Works
1. On admin: open Testing page → Serial Monitor section
2. On node: wait for REGISTER to be sent
3. Admin should show: `RX <- [0x90] from XXXX (...bytes) {"cmd":"REGISTER",...}`
4. If admin shows nothing → node's XBee is not transmitting, or XBees aren't associated

### Step 4: Verify RX Works
1. On admin: click "Send Test Message"
2. Node serial monitor should show: `[XBee] RX 0x90 (...bytes): {"cmd":"BROADCAST_MSG",...}`
3. If node shows nothing → check `gpio_reset_pin()` fix, check wiring

### Step 5: Monitor Full Handshake
```
Node serial output:
  [Node] Sent REGISTER
  [XBee] TX status: OK (frame 1)        ← XBee accepted our frame
  [XBee] RX 0x90 (28 bytes): {"cmd":"REGISTER_ACK","node_id":"node-01"}
  [Node] Got REGISTER_ACK — registered!
  [Node] Sent SYNC_REQUEST
  [XBee] RX 0x90 (1234 bytes): {"cmd":"SYNC_DATA","users":[...],...}
  [Node] Sync complete — now in RUNNING state
```

---

## 17. Implementation Checklist

### Phase 1 — XBee Communication (Get REGISTER working)
- [ ] Create `platformio.ini` with ESP32-CAM + libraries
- [ ] Create `config.h` with pin definitions and node identity
- [ ] Create `xbee_comm.h` and `xbee_comm.cpp` (API mode 1 driver)
  - [ ] **Include `gpio_reset_pin()` fix** (most critical!)
  - [ ] `xbeeInit()` — UART2 setup with gpio fix
  - [ ] `xbeeSendBroadcast()` — build 0x10 TX Request frame
  - [ ] `xbeeProcessIncoming()` — state machine to parse 0x90 RX frames
- [ ] Create `sd_storage.h` and `sd_storage.cpp`
- [ ] Create `node_client.h` and `node_client.cpp`
  - [ ] `sendRegister()` — send REGISTER every 10s
  - [ ] `handleRegisterAck()` — transition to registered state
- [ ] Create minimal `main.cpp` (SD init + WiFi AP + XBee + node client)
- [ ] **Test:** Flash, open serial monitor, verify "Sent REGISTER" appears
- [ ] **Test:** Check admin serial monitor — should show 0x90 frame with REGISTER

### Phase 2 — Data Sync
- [ ] Implement `handleSyncData()` — save SYNC_DATA to SD card files
- [ ] Implement `sendSyncRequest()` — request full data after REGISTER_ACK
- [ ] Implement `sendHeartbeat()` — 30-second heartbeat cycle
- [ ] Implement `handleBroadcastMsg()` — save admin broadcasts to SD
- [ ] Implement `handleGetStats()` — respond with node stats
- [ ] **Test:** Admin serial monitor shows SYNC_REQUEST, node receives SYNC_DATA

### Phase 3 — Mobile App API
- [ ] Add ESPAsyncWebServer with mobile API endpoints
- [ ] `GET /status` → `{"online": true}`
- [ ] `POST /login` → authenticate from synced users.json
- [ ] `GET /announcements` → return from synced announcements.json
- [ ] `GET /users`, `GET /conversations`, `GET /messages`, `GET /new-messages`
- [ ] `POST /send` → store locally + relay via `RELAY_CHAT_MSG`
- [ ] `POST /sos` → relay via `SOS_ALERT`
- [ ] `POST /create-chat` → store locally
- [ ] `POST /change-password` → relay via `CHANGE_PASSWORD`
- [ ] `POST /agree-sos` → update synced users.json locally
- [ ] **Test:** Connect phone to node WiFi, open HopFogMobile app

---

## Appendix: Admin Diagnostic Endpoints

If you need to debug from the admin side, these endpoints are available on the admin web UI:

| Endpoint | Method | What it does |
|----------|--------|-------------|
| `GET /api/xbee/rx-log` | GET | Returns last 50 serial events (TX, RX, errors) |
| `GET /api/xbee/diagnostics` | GET | Full diagnostic with GPIO states, hex dump, assessment |
| `POST /api/xbee/test` | POST | Send a test broadcast message |
| `POST /api/xbee/send-raw` | POST | Send arbitrary text via XBee |
| `GET /api/nodes` | GET | List registered nodes |
| `POST /api/nodes/{id}/sync` | POST | Manually trigger SYNC_DATA to a node |
| `POST /api/nodes/{id}/get-stats` | POST | Request stats from a node |
