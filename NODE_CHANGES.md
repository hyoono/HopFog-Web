# HopFog-Node — Critical Fix: XBee Payload Size Limit

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
> Branch: `copilot/setup-and-xbee-driver`
>
> **THIS IS THE FIX.** All previous NODE_CHANGES (#1–#9) were trying to fix
> init order, UART config, etc. — but the real problem was **payload size**.

---

## ROOT CAUSE: ZigBee Broadcast Payload Limit

**XBee S2C ZigBee broadcast max RF payload: ~84 bytes.**
Broadcast frames CANNOT be fragmented. Messages larger than ~84 bytes are
**silently dropped** by the XBee module — no error, no TX FAIL, nothing.

### Message Size Analysis

| Message | Bytes | Limit | Result |
|---------|-------|-------|--------|
| PING | ~35 | 84 | ✅ Works |
| PONG | ~31 | 84 | ✅ Works |
| REGISTER_ACK | ~44 | 84 | ✅ Works |
| **Node REGISTER** | **~155** | 84 | ❌ **Silently dropped** |
| **Node HEARTBEAT** | **~120** | 84 | ❌ **Silently dropped** |
| **SYNC_REQUEST** | ~55 | 84 | ✅ Works |
| **SYNC_DATA (empty)** | **~160** | 84 | ❌ **Silently dropped** |

This is why:
- XCTU registration works (small messages fit)
- The test project works (small messages fit)
- The real node doesn't register (REGISTER is ~155 bytes = DROPPED)
- Sync never works (SYNC_DATA is ~160+ bytes = DROPPED)

---

## Fix Summary

1. **Trim REGISTER to < 72 bytes** — remove ip_address, status, free_heap
2. **Trim HEARTBEAT to < 72 bytes** — use short keys, drop ip_address
3. **Remove `ts` field** from sendCommand — saves ~10 bytes per message
4. **Shorter DEVICE_NAME** — "Node01" instead of "HopFog-Node-01"
5. **Handle chunked SYNC_DATA** — admin sends SYNC_DATA with `part` field
6. **Handle SYNC_DONE** — new command marking end of chunked sync
7. **Handle PING** — admin sends PING every 10s, node replies PONG

---

## Required Changes

### Change 1: Trim `sendCommand()` in `src/node_client.cpp`

Remove the `ts` field to save ~10 bytes per message:

```cpp
static void sendCommand(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    // REMOVED: doc["ts"] = (long)(millis() / 1000);
    // ts adds ~10 bytes and pushes messages over the 72-byte broadcast limit
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}
```

### Change 2: Trim `sendRegister()` in `src/node_client.cpp`

```cpp
static void sendRegister() {
    // MUST be < 72 bytes total for ZigBee broadcast!
    // Old version was ~155 bytes and was SILENTLY DROPPED by XBee.
    JsonDocument doc;
    doc["cmd"] = "REGISTER";
    JsonObject p = doc["params"].to<JsonObject>();
    p["name"] = DEVICE_NAME;
    // DO NOT add ip_address, status, free_heap — makes payload too large
    sendCommand(doc);
}
```

**Output:** `{"cmd":"REGISTER","params":{"name":"Node01"},"node_id":"node-01"}` = ~63 bytes ✅

### Change 3: Trim `sendHeartbeat()` in `src/node_client.cpp`

```cpp
static void sendHeartbeat() {
    // MUST be < 72 bytes for ZigBee broadcast
    JsonDocument doc;
    doc["cmd"] = "HEARTBEAT";
    JsonObject p = doc["params"].to<JsonObject>();
    p["up"] = (int)(millis() / 1000);
    p["heap"] = (int)(ESP.getFreeHeap() / 1024);  // KB not bytes
    sendCommand(doc);
}
```

**Output:** `{"cmd":"HEARTBEAT","params":{"up":123,"heap":180},"node_id":"node-01"}` = ~67 bytes ✅

### Change 4: Shorter DEVICE_NAME in `include/config.h`

```cpp
#define DEVICE_NAME   "Node01"   // Was "HopFog-Node-01" — too long for broadcast limit
```

### Change 5: Handle chunked SYNC_DATA in `src/node_client.cpp`

The admin now sends SYNC_DATA in parts, not as one giant message:

```json
{"cmd":"SYNC_DATA","node_id":"node-01","part":"users","data":[...]}
{"cmd":"SYNC_DATA","node_id":"node-01","part":"announcements","data":[...]}
...
{"cmd":"SYNC_DONE","node_id":"node-01"}
```

**Replace `handleSyncData()` with:**

```cpp
static void handleSyncData(JsonDocument& doc) {
    const char* part = doc["part"] | "";

    if (strlen(part) == 0) {
        // Legacy single SYNC_DATA (backward compatible)
        if (doc["users"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["users"]);
            writeJsonFile(SD_USERS_FILE, d);
        }
        if (doc["announcements"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["announcements"]);
            writeJsonFile(SD_ANNOUNCE_FILE, d);
        }
        if (doc["conversations"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["conversations"]);
            writeJsonFile(SD_CONVOS_FILE, d);
        }
        if (doc["chat_messages"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["chat_messages"]);
            writeJsonFile(SD_DMS_FILE, d);
        }
        if (doc["fog_nodes"].is<JsonArray>()) {
            JsonDocument d; d.set(doc["fog_nodes"]);
            writeJsonFile(SD_FOG_FILE, d);
        }
        state = STATE_RUNNING;
        lastHeartbeatMs = millis();
        return;
    }

    // Chunked SYNC_DATA (new format)
    JsonDocument saveDoc;
    if (doc["data"].is<JsonArray>()) {
        saveDoc.set(doc["data"]);
    } else {
        saveDoc.to<JsonArray>();
    }

    if (strcmp(part, "users") == 0) {
        writeJsonFile(SD_USERS_FILE, saveDoc);
    } else if (strcmp(part, "announcements") == 0) {
        writeJsonFile(SD_ANNOUNCE_FILE, saveDoc);
    } else if (strcmp(part, "conversations") == 0) {
        writeJsonFile(SD_CONVOS_FILE, saveDoc);
    } else if (strcmp(part, "chat_messages") == 0) {
        writeJsonFile(SD_DMS_FILE, saveDoc);
    } else if (strcmp(part, "fog_nodes") == 0) {
        writeJsonFile(SD_FOG_FILE, saveDoc);
    }
}

static void handleSyncDone() {
    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
}
```

### Change 6: Handle PING and SYNC_DONE in `nodeClientHandleCommand()`

Add these cases to the command dispatcher:

```cpp
    } else if (strcmp(cmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(cmd, "SYNC_DONE") == 0) {
        handleSyncDone();
    } else if (strcmp(cmd, "PING") == 0) {
        // Admin sends PING every 10s — reply with PONG
        JsonDocument pong;
        pong["cmd"] = "PONG";
        sendCommand(pong);
    } else if (strcmp(cmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(cmd, "BROADCAST_MSG") == 0) {
```

### Change 7: Trim `handleGetStats()` in `src/node_client.cpp`

```cpp
static void handleGetStats() {
    JsonDocument doc;
    doc["cmd"] = "STATS_RESPONSE";
    JsonObject p = doc["params"].to<JsonObject>();
    p["heap"] = (int)(ESP.getFreeHeap() / 1024);
    p["up"] = (int)(millis() / 1000);
    sendCommand(doc);
}
```

---

## What the Admin Side Changed

1. **Unicast replies**: Admin sends REGISTER_ACK, PONG, SYNC_DATA via unicast
   to the node's specific XBee address (from 0x90 frame source address).
   Unicast supports fragmentation up to ~255 bytes.

2. **Broadcast only for PING**: Only the admin's periodic PING uses broadcast.

3. **Chunked SYNC_DATA**: Multiple small chunks with `part` field, ended by
   `SYNC_DONE`.

4. **Payload size warning**: `xbeeSendBroadcast()` logs "OVERSIZED BROADCAST!"
   when payload exceeds 72 bytes.

5. **TX Status decoding**: TX FAIL 0x74 decoded as "PAYLOAD TOO LARGE".

6. **Manual trigger buttons**: Admin testing page has buttons to manually send
   PING, REGISTER_ACK, PONG, GET_STATS, SYNC_DATA, etc.

---

## XCTU Configuration — 2 Modules

### Admin XBee (Coordinator)

| Parameter | Value |
|-----------|-------|
| CE | 1 (Coordinator) |
| AP | 1 (API Mode 1) |
| BD | 3 (9600 baud) |
| ID | 1234 (PAN ID) |
| DH | 0 |
| DL | FFFF |

### Node XBee (Router)

| Parameter | Value |
|-----------|-------|
| CE | 0 (Router) |
| JV | 1 (Channel Verify) |
| AP | 1 (API Mode 1) |
| BD | 3 (9600 baud) |
| ID | 1234 (PAN ID) |
| DH | 0 |
| DL | FFFF |

---

## ALSO IMPORTANT: ASYNCWEBSERVER_REGEX

If the node uses ESPAsyncWebServer with any regex routes (e.g. `"^\\/api\\/path\\/(\\d+)$"`),
you MUST add `-DASYNCWEBSERVER_REGEX=1` to `build_flags` in `platformio.ini`.

Without this flag, regex routes **silently don't match** → 404.

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM=1
    -DASYNCWEBSERVER_REGEX=1
```

---

## Verification After Flashing

1. Admin serial monitor: `TX OK` for PINGs, `RX ←` for node REGISTER
2. Node status: state progresses 0→1→2→3
3. No "OVERSIZED BROADCAST!" errors in admin log