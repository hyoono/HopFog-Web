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

### Change 5: Handle SYNC_DATA and SC (Sync Continuation) in `src/node_client.cpp`

The admin sends SYNC_DATA **one record at a time**. ALL fields are included
(password_hash, body, message_text — nothing is stripped).

**Small records** (<240 bytes) are sent in a single message:
```json
{"cmd":"SYNC_DATA","node_id":"node-01","part":"users","seq":0,"d":{"id":1,"username":"admin","password_hash":"$2b$10$...","role":"admin"}}
```

**Large records** (>240 bytes) are split: the base record has long text fields
emptied, followed by SC (Sync Continuation) messages with the full text:
```json
{"cmd":"SYNC_DATA","node_id":"node-01","part":"announcements","seq":0,"d":{"id":1,"subject":"","body":"","status":"sent","created_at":1709900000}}
{"cmd":"SC","p":"announcements","s":0,"k":"subject","v":"Emergency Evacuation Notice"}
{"cmd":"SC","p":"announcements","s":0,"k":"body","v":"first 130 chars of body text..."}
{"cmd":"SC","p":"announcements","s":0,"k":"body","v":"next 130 chars of body text..."}
```

**Count** message (no "d" field): `{"cmd":"SYNC_DATA",...,"part":"users","n":3}`
**Final**: `{"cmd":"SYNC_DONE","node_id":"node-01"}`

**You need in-memory buffers to collect records per part:**

```cpp
// Add at the top of node_client.cpp (file scope)
#include <ArduinoJson.h>

// Sync accumulation buffers — one JsonDocument per data part
static JsonDocument syncUsers;
static JsonDocument syncAnnouncements;
static JsonDocument syncConversations;
static JsonDocument syncChatMessages;
static JsonDocument syncFogNodes;

static void initSyncBuffers() {
    syncUsers.to<JsonArray>();
    syncAnnouncements.to<JsonArray>();
    syncConversations.to<JsonArray>();
    syncChatMessages.to<JsonArray>();
    syncFogNodes.to<JsonArray>();
}

// Helper: find the sync buffer for a given part name
static JsonDocument* getSyncBuffer(const char* part) {
    if (strcmp(part, "users") == 0) return &syncUsers;
    if (strcmp(part, "announcements") == 0) return &syncAnnouncements;
    if (strcmp(part, "conversations") == 0) return &syncConversations;
    if (strcmp(part, "chat_messages") == 0) return &syncChatMessages;
    if (strcmp(part, "fog_nodes") == 0) return &syncFogNodes;
    return nullptr;
}
```

**Replace `handleSyncData()` with:**

```cpp
static void handleSyncData(JsonDocument& doc) {
    const char* part = doc["part"] | "";
    if (strlen(part) == 0) return;

    // Count message (has "n") — save accumulated buffer to SD
    if (doc["n"].is<int>()) {
        JsonDocument* buf = getSyncBuffer(part);
        if (buf) {
            // Use your existing writeJsonFile or writeJsonArray function.
            // Map part name to your SD file path constant:
            const char* path = nullptr;
            if (strcmp(part, "users") == 0) path = SD_USERS_FILE;
            else if (strcmp(part, "announcements") == 0) path = SD_ANNOUNCE_FILE;
            else if (strcmp(part, "conversations") == 0) path = SD_CONVOS_FILE;
            else if (strcmp(part, "chat_messages") == 0) path = SD_DMS_FILE;
            else if (strcmp(part, "fog_nodes") == 0) path = SD_FOG_FILE;
            if (path) writeJsonFile(path, *buf);
        }
        return;
    }

    // Regular record message (has "d")
    if (!doc["d"].is<JsonObject>()) return;
    JsonObject record = doc["d"].as<JsonObject>();

    JsonDocument* buf = getSyncBuffer(part);
    if (buf) {
        buf->as<JsonArray>().add(record);
    }
}
```

**NEW: Add `handleSyncContinuation()` for SC messages:**

```cpp
static void handleSyncContinuation(JsonDocument& doc) {
    // SC = Sync Continuation: appends text to a field in a previously
    // received record. Used for long text fields that exceeded the
    // 240-byte XBee unicast limit.
    //
    // Format: {"cmd":"SC","p":"announcements","s":0,"k":"body","v":"...text chunk..."}
    //   p = part name
    //   s = seq number (index into the sync buffer for that part)
    //   k = field key to append to
    //   v = text chunk to append

    const char* part = doc["p"] | "";
    int seq = doc["s"] | -1;
    const char* key = doc["k"] | "";
    const char* val = doc["v"] | "";
    if (seq < 0 || strlen(key) == 0 || strlen(part) == 0) return;

    JsonDocument* buf = getSyncBuffer(part);
    if (!buf) return;

    JsonArray arr = buf->as<JsonArray>();
    if (seq >= (int)arr.size()) return;  // Record not yet received

    JsonObject rec = arr[seq].as<JsonObject>();
    // Append v to existing value of key
    String existing = rec[key].as<String>();
    existing += val;
    rec[key] = existing;
}

static void handleSyncDone() {
    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
    initSyncBuffers();
}
```

**Add SC handling in the command dispatcher:**

```cpp
    } else if (strcmp(cmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(cmd, "SC") == 0) {
        handleSyncContinuation(doc);
    } else if (strcmp(cmd, "SYNC_DONE") == 0) {
        handleSyncDone();
```

**Call `initSyncBuffers()` in your `nodeClientInit()` and at the start of
`handleSyncRequest()` (when sending SYNC_REQUEST):**

```cpp
void nodeClientInit() {
    state = STATE_UNREGISTERED;
    initSyncBuffers();
    // ... rest of init ...
}

// Also in sendSyncRequest():
static void sendSyncRequest() {
    initSyncBuffers();  // Clear buffers before receiving new sync
    JsonDocument doc;
    doc["cmd"] = "SYNC_REQUEST";
    sendCommand(doc);
    state = STATE_SYNCING;
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

3. **ALL fields included in sync**: Password hashes, full body text, full
   message text — nothing is stripped or truncated.

4. **SC (Sync Continuation) for large records**: When a record exceeds the
   240-byte unicast limit, long text fields (>30 chars) are emptied in the
   base SYNC_DATA message, and the full text is sent in separate SC messages:
   `{"cmd":"SC","p":"announcements","s":0,"k":"body","v":"...text chunk..."}`
   The node appends each chunk to the corresponding record+field.

5. **Payload size warning**: `xbeeSendBroadcast()` logs "OVERSIZED BROADCAST!"
   when payload exceeds 72 bytes.

6. **TX Status decoding**: TX FAIL 0x74 decoded as "PAYLOAD TOO LARGE".

7. **Manual trigger buttons**: Admin testing page has buttons to manually send
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