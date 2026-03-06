# HopFog-Node — Build From Scratch (Part 2 of 3: Node Client & Storage)

> **For:** Copilot agent (Claude Opus 4.6) working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
>
> **Prerequisites:** Complete Part 1 first (config.h, xbee_comm.h, xbee_comm.cpp).
>
> **This part covers:** The node protocol client state machine and SD card storage.

---

## 9. Implementation: node_client.h

The node protocol client handles the state machine for registration, heartbeat, and sync.

```cpp
#ifndef NODE_CLIENT_H
#define NODE_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Node state ─────────────────────────────────────────────────────
enum NodeState {
    STATE_UNREGISTERED,   // Sending REGISTER every 10s
    STATE_REGISTERED,     // Got REGISTER_ACK, now syncing
    STATE_SYNCING,        // Sent SYNC_REQUEST, waiting for SYNC_DATA
    STATE_RUNNING         // Fully synced, sending HEARTBEAT every 30s
};

/// Initialize the node client (call once in setup, after xbeeInit).
void nodeClientInit();

/// Call from loop() — handles timers for REGISTER/HEARTBEAT/SYNC.
void nodeClientLoop();

/// Handle an incoming JSON command from admin (call from XBee RX callback).
/// Returns true if it was a recognized admin command.
bool nodeClientHandleCommand(const char* payload, size_t len);

/// Get current state (for diagnostic display).
NodeState nodeClientGetState();

#endif // NODE_CLIENT_H
```

---

## 10. Implementation: node_client.cpp

```cpp
#include "node_client.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"

static NodeState state = STATE_UNREGISTERED;
static unsigned long lastRegisterMs  = 0;
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastSyncMs      = 0;

// ── Helper: send a JSON command via XBee ────────────────────────────
static void sendCommand(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Outgoing commands ───────────────────────────────────────────────

static void sendRegister() {
    JsonDocument doc;
    doc["cmd"] = "REGISTER";
    JsonObject params = doc["params"].to<JsonObject>();
    params["device_name"] = DEVICE_NAME;
    params["ip_address"] = WiFi.softAPIP().toString();
    params["status"] = "active";
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
    Serial.println("[Node] Sent REGISTER");
}

static void sendHeartbeat() {
    JsonDocument doc;
    doc["cmd"] = "HEARTBEAT";
    JsonObject params = doc["params"].to<JsonObject>();
    params["ip_address"] = WiFi.softAPIP().toString();
    params["uptime"] = (int)(millis() / 1000);
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
}

static void sendSyncRequest() {
    JsonDocument doc;
    doc["cmd"] = "SYNC_REQUEST";
    sendCommand(doc);
    Serial.println("[Node] Sent SYNC_REQUEST");
}

// ── Incoming command handlers ───────────────────────────────────────

static void handleRegisterAck() {
    Serial.println("[Node] Got REGISTER_ACK — registered with admin!");
    state = STATE_REGISTERED;
    // Immediately request data sync
    sendSyncRequest();
    lastSyncMs = millis();
    state = STATE_SYNCING;
}

static void handlePong() {
    Serial.println("[Node] Got PONG");
}

static void handleSyncData(JsonDocument& doc) {
    Serial.println("[Node] Got SYNC_DATA — saving to SD card...");

    // Save each data category to its own file
    if (doc["users"].is<JsonArray>()) {
        JsonDocument usersDoc;
        usersDoc.set(doc["users"]);
        writeJsonFile(SD_USERS_FILE, usersDoc);
        Serial.printf("[Node] Saved %d users\n", doc["users"].as<JsonArray>().size());
    }

    if (doc["announcements"].is<JsonArray>()) {
        JsonDocument annDoc;
        annDoc.set(doc["announcements"]);
        writeJsonFile(SD_ANNOUNCE_FILE, annDoc);
        Serial.printf("[Node] Saved %d announcements\n", doc["announcements"].as<JsonArray>().size());
    }

    if (doc["conversations"].is<JsonArray>()) {
        JsonDocument convDoc;
        convDoc.set(doc["conversations"]);
        writeJsonFile(SD_CONVOS_FILE, convDoc);
    }

    if (doc["chat_messages"].is<JsonArray>()) {
        JsonDocument dmDoc;
        dmDoc.set(doc["chat_messages"]);
        writeJsonFile(SD_DMS_FILE, dmDoc);
    }

    if (doc["fog_nodes"].is<JsonArray>()) {
        JsonDocument fogDoc;
        fogDoc.set(doc["fog_nodes"]);
        writeJsonFile(SD_FOG_FILE, fogDoc);
    }

    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
    Serial.println("[Node] Sync complete — now in RUNNING state");
}

static void handleBroadcastMsg(JsonObject params) {
    // Admin sent a broadcast announcement — store it locally
    Serial.printf("[Node] Broadcast: %s\n", (const char*)(params["message"] | ""));

    JsonDocument doc;
    readJsonFile(SD_ANNOUNCE_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject a : arr) {
        int id = a["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonObject ann = arr.add<JsonObject>();
    ann["id"] = newId;
    ann["title"] = params["subject"] | params["message"] | "";
    ann["message"] = params["message"] | "";
    ann["created_at"] = String((long)(millis() / 1000));

    writeJsonFile(SD_ANNOUNCE_FILE, doc);
}

static void handleGetStats() {
    Serial.println("[Node] Admin requested stats");
    JsonDocument doc;
    doc["cmd"] = "STATS_RESPONSE";
    JsonObject params = doc["params"].to<JsonObject>();
    params["free_heap"] = (int)ESP.getFreeHeap();
    params["uptime"] = (int)(millis() / 1000);
    params["ip_address"] = WiFi.softAPIP().toString();
    params["wifi_stations"] = WiFi.softAPgetStationNum();
    sendCommand(doc);
}

// ── Public API ──────────────────────────────────────────────────────

void nodeClientInit() {
    state = STATE_UNREGISTERED;
    lastRegisterMs = 0;
    lastHeartbeatMs = 0;
    lastSyncMs = 0;
    Serial.println("[Node] Client initialized — will start REGISTER cycle");
}

void nodeClientLoop() {
    unsigned long now = millis();

    switch (state) {
    case STATE_UNREGISTERED:
        // Send REGISTER every 10 seconds until we get ACK
        if (now - lastRegisterMs >= REGISTER_INTERVAL_MS) {
            sendRegister();
            lastRegisterMs = now;
        }
        break;

    case STATE_REGISTERED:
        // Just registered — waiting to start sync (handled in handleRegisterAck)
        break;

    case STATE_SYNCING:
        // Waiting for SYNC_DATA — retry if no response
        if (now - lastSyncMs >= SYNC_RETRY_MS) {
            sendSyncRequest();
            lastSyncMs = now;
        }
        break;

    case STATE_RUNNING:
        // Send HEARTBEAT every 30 seconds
        if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
            sendHeartbeat();
            lastHeartbeatMs = now;
        }
        break;
    }
}

bool nodeClientHandleCommand(const char* payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;

    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    Serial.printf("[Node] RX cmd: %s\n", cmd);

    if (strcmp(cmd, "REGISTER_ACK") == 0) {
        handleRegisterAck();
    } else if (strcmp(cmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(cmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(cmd, "BROADCAST_MSG") == 0) {
        handleBroadcastMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(cmd, "GET_STATS") == 0) {
        handleGetStats();
    } else {
        Serial.printf("[Node] Unknown admin command: %s\n", cmd);
        return false;
    }
    return true;
}

NodeState nodeClientGetState() {
    return state;
}
```

---

## 11. Implementation: sd_storage.h / .cpp

### sd_storage.h

```cpp
#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

/// Initialize the SD card. Returns true on success.
bool initSDCard();

/// Read a JSON file from SD into a JsonDocument.
/// Returns true if file exists and was parsed.
bool readJsonFile(const char* path, JsonDocument& doc);

/// Write a JsonDocument to a file on SD (overwrites).
bool writeJsonFile(const char* path, JsonDocument& doc);

#endif // SD_STORAGE_H
```

### sd_storage.cpp

```cpp
#include "sd_storage.h"
#include "config.h"
#include <SD_MMC.h>

bool initSDCard() {
    // ESP32-CAM flash LED (GPIO 4) can interfere with SD bus — keep it OFF
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("[SD] SD_MMC init FAILED");
        return false;
    }

    Serial.printf("[SD] Card mounted — size: %lluMB\n",
                  SD_MMC.totalBytes() / (1024 * 1024));

    // Create /db directory if it doesn't exist
    if (!SD_MMC.exists(SD_DB_DIR)) {
        SD_MMC.mkdir(SD_DB_DIR);
    }

    // Create default empty JSON files if they don't exist
    const char* files[] = {
        SD_USERS_FILE, SD_ANNOUNCE_FILE, SD_CONVOS_FILE,
        SD_DMS_FILE, SD_FOG_FILE, SD_MSGS_FILE
    };
    for (const char* f : files) {
        if (!SD_MMC.exists(f)) {
            File file = SD_MMC.open(f, FILE_WRITE);
            if (file) {
                file.print("[]");
                file.close();
            }
        }
    }

    return true;
}

bool readJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[SD] File not found: %s\n", path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[SD] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

bool writeJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[SD] Cannot write: %s\n", path);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
```

---

