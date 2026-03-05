# Notes for HopFog-Node Copilot Agent

> **Context:** These notes are for the Copilot agent working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node) branch `copilot/add-xbee-node-functionality`. The admin repo ([hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)) has been updated to be fully compatible with the node's XBee protocol.

---

## What Changed on the Admin Side

The admin ESP32 firmware was previously using **XBee API mode 1** (binary frames with 0x7E start byte, checksums, etc.). This was **incompatible** with the node's AT/transparent mode (`Serial.println(json)`).

**The admin has now been switched to AT/transparent mode (AP=0)** to match the node exactly:

| Feature | Before (broken) | After (compatible) |
|---------|-----------------|-------------------|
| XBee mode | API mode 1 (binary frames) | AT/transparent (text) |
| How admin sends | `sendFrame(0x7E, len, payload, checksum)` | `Serial.println(json)` |
| How admin receives | Parse 0x7E binary frames | Buffer until `\n`, parse JSON |
| XBee module config | AP=1 | AP=0, DH=0, DL=FFFF |

**The admin now speaks the exact same newline-delimited JSON protocol as the node.**

---

## XBee Module Configuration (All Modules Must Match)

| Parameter | Admin XBee | Node XBee | Description |
|-----------|-----------|-----------|-------------|
| **AP** | `0` (Transparent) | `0` (Transparent) | AT mode — raw text passthrough |
| **CE** | `1` (Coordinator) | `0` (Router) | One coordinator, multiple routers |
| **ID** | `1234` | `1234` | Same PAN ID on all devices |
| **BD** | `3` (9600) | `3` (9600) | Baud rate |
| **DH** | `0` | `0` | Broadcast destination high |
| **DL** | `FFFF` | `FFFF` | Broadcast destination low |

---

## Protocol Compatibility Verification

### Commands the admin now handles (Node → Admin)

| Command | Admin Response | Status |
|---------|---------------|--------|
| `REGISTER` | `REGISTER_ACK` | ✅ Working |
| `HEARTBEAT` | `PONG` | ✅ Working |
| `SYNC_REQUEST` | `SYNC_DATA` (users, announcements, conversations, chat_messages, fog_nodes) | ✅ Working |
| `RELAY_MSG` | _(logged)_ | ✅ Working |
| `RELAY_FOG_NODE` | _(stored in fog_devices.json)_ | ✅ Working |
| `RELAY_CHAT_MSG` | _(stored in direct_messages.json)_ | ✅ Working |
| `SOS_ALERT` | _(creates SOS request in resident_admin_msgs.json)_ | ✅ Working |
| `CHANGE_PASSWORD` | _(updates user in users.json)_ | ✅ Working |
| `STATS_RESPONSE` | _(updates node registry in memory)_ | ✅ Working |

### Commands the admin now sends (Admin → Node)

| Command | When Sent | Status |
|---------|-----------|--------|
| `REGISTER_ACK` | Reply to REGISTER | ✅ Working |
| `PONG` | Reply to HEARTBEAT | ✅ Working |
| `SYNC_DATA` | Reply to SYNC_REQUEST | ✅ Working |
| `BROADCAST_MSG` | Admin creates/sends a broadcast | ✅ Working |
| `GET_STATS` | Admin requests node stats via web UI | ✅ Working |
| `SOS_ALERT` (relay) | Admin escalates SOS to XBee | ✅ Working |
| `RELAY_CHAT_MSG` (relay) | Mobile user sends DM via admin | ✅ Working |

---

## Things the Node Agent Should Verify

### 1. The node already works correctly — no changes needed for basic protocol

The node code in `src/main.cpp` already:
- Uses `xbeeSerial.println(json)` to send ✅
- Buffers incoming bytes until `\n` to receive ✅
- Parses JSON with ArduinoJson ✅
- Handles PONG, REGISTER_ACK, SYNC_DATA, BROADCAST_MSG, ADD_FOG_NODE, GET_STATS ✅

### 2. Verify the `StaticJsonDocument<1024>` size is sufficient

The admin's `SYNC_DATA` response can be large (users + announcements + conversations + chat_messages + fog_nodes). If there are many users/messages, the JSON payload may exceed:

- The XBee's single-frame RF limit (~256 bytes for transparent mode)
- The ArduinoJson document size on the node (`StaticJsonDocument<1024>`)

**Note on large payloads:** In AT mode, XBee automatically fragments long serial data into multiple RF frames using the RO (Packetization Timeout) parameter. The node's line-buffer approach (read until `\n`) should reassemble correctly in most cases since XBee delivers RF fragments in order. However, if packets are lost mid-line or there are timing gaps, the node may receive a partial line. The node should handle JSON parse failures gracefully (which it already does by checking `deserializeJson` return value).

**Recommendation:** If SYNC_DATA fails for large datasets, increase the JsonDocument size on the node or have the admin send smaller chunked syncs.

### 3. The `containsKey()` deprecation

The node uses `doc.containsKey("key")` which is deprecated in ArduinoJson v7+. The admin has already migrated to `doc["key"].is<JsonVariant>()`. If the node uses ArduinoJson v7+, consider updating:

```cpp
// Old (deprecated in v7):
if (doc.containsKey("users")) { ... }

// New (v7+):
if (doc["users"].is<JsonArray>()) { ... }
```

### 4. No changes needed to the node's XBee configuration

The node's XBee should already be in AT mode (AP=0) since it uses `Serial.println()`. Just verify:
- AP=0 (transparent mode)
- CE=0 (router, not coordinator)
- DH=0, DL=FFFF (broadcast)
- Same PAN ID as the admin's XBee

---

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        ADMIN ESP32                          │
│                                                             │
│  Web UI (browser) ──HTTP──► API Handlers ──► SD Card JSON   │
│                                   │                         │
│                                   ▼                         │
│                             xbeeSendBroadcast()             │
│                                   │                         │
│                     Serial.println(json) + '\n'             │
│                                   │                         │
│                              UART2 TX                       │
└───────────────────────────────────┼─────────────────────────┘
                                    │
                              XBee S2C Pro
                            (AP=0, CE=1, DL=FFFF)
                                    │
                              ~~~~ RF ~~~~
                                    │
                              XBee S2C
                            (AP=0, CE=0, DL=FFFF)
                                    │
┌───────────────────────────────────┼─────────────────────────┐
│                              UART RX                        │
│                                   │                         │
│                     Buffer until '\n'                       │
│                                   │                         │
│                         handleXBeeData(line)                │
│                                   │                         │
│                     Parse JSON, dispatch command             │
│                                                             │
│                         NODE ESP32                           │
│  Mobile App ──HTTP──► Node API ──► Local Storage            │
└─────────────────────────────────────────────────────────────┘
```

---

## SYNC_DATA Payload Schema (what the admin sends)

```json
{
  "cmd": "SYNC_DATA",
  "node_id": "node-01",
  "users": [
    {"id": 1, "username": "admin", "email": "admin@hopfog.com", "role": "admin", "is_active": true, "has_agreed_sos": true}
  ],
  "announcements": [
    {"id": 1, "title": "Test", "message": "Test announcement", "created_at": "1709000000"}
  ],
  "conversations": [
    {"id": 1, "user1_id": 1, "user2_id": 2, "is_sos": false, "created_at": 1709000000}
  ],
  "chat_messages": [
    {"id": 1, "conversation_id": 1, "sender_id": 2, "message_text": "Hello", "sent_at": 1709000000}
  ],
  "fog_nodes": [
    {"id": 1, "device_name": "node-01", "status": "active"}
  ],
  "messages": []
}
```

---

## Summary

**The admin is now fully compatible with the node. No protocol changes needed on the node side.** The only potential issues are:

1. Large SYNC_DATA payloads exceeding JsonDocument size on the node
2. ArduinoJson v7 deprecation warnings (optional cleanup)
3. XBee module configuration — ensure AP=0 on ALL modules
