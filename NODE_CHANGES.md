# HopFog-Node — Complete Alignment Guide

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
> Branch: `copilot/setup-and-xbee-driver`
>
> **This supersedes ALL previous NODE_CHANGES documents.**

---

## Issue 1: Login Fails (CRITICAL)

The node's POST /login does plaintext password comparison:
```cpp
strcmp(user["password"] | "", password) == 0  // ← WRONG
```

But admin stores passwords as `password_hash` with format `"salt:sha256hex"`.
After sync, users.json has `password_hash` field, not `password`. Login always fails.

### Fix: Add SHA-256 Password Verification

**Create `include/auth.h`:**

```cpp
#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

String hashPassword(const String& password);
bool verifyPassword(const String& password, const String& storedHash);

#endif
```

**Create `src/auth.cpp`:**

```cpp
#include "auth.h"
#include <mbedtls/sha256.h>
#include <esp_random.h>

static String sha256Hex(const String& input) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx,
        (const unsigned char*)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    String hex;
    hex.reserve(64);
    for (int i = 0; i < 32; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        hex += buf;
    }
    return hex;
}

static String generateSalt() {
    String salt;
    salt.reserve(16);
    for (int i = 0; i < 8; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", (uint8_t)esp_random());
        salt += buf;
    }
    return salt;
}

String hashPassword(const String& password) {
    String salt = generateSalt();
    String hash = sha256Hex(salt + ":" + password);
    return salt + ":" + hash;
}

bool verifyPassword(const String& password, const String& storedHash) {
    int sep = storedHash.indexOf(':');
    if (sep < 0) return false;
    String salt = storedHash.substring(0, sep);
    String expectedHash = storedHash.substring(sep + 1);
    String computedHash = sha256Hex(salt + ":" + password);
    return computedHash == expectedHash;
}
```

**Fix POST /login in `src/api_handlers.cpp`:**

Add `#include "auth.h"` at the top, then replace the login handler body:

```cpp
server.on("/login", HTTP_POST, [](AsyncWebServerRequest* request) {
}, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
            size_t index, size_t total) {
    JsonDocument reqDoc;
    DeserializationError err = deserializeJson(reqDoc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* username = reqDoc["username"];
    const char* password = reqDoc["password"];
    if (!username || !password) {
        request->send(400, "application/json",
                      "{\"error\":\"Missing username or password\"}");
        return;
    }

    JsonDocument usersDoc;
    readJsonFile(SD_USERS_FILE, usersDoc);
    JsonArray users = usersDoc.as<JsonArray>();

    for (JsonObject user : users) {
        if (strcmp(user["username"] | "", username) != 0) continue;

        // Check is_active
        int active = user["is_active"] | 1;
        if (active == 0) {
            request->send(403, "application/json",
                          "{\"success\":false,\"error\":\"Account inactive\"}");
            return;
        }

        // Verify password against password_hash (salt:sha256hex format)
        const char* storedHash = user["password_hash"] | "";
        if (strlen(storedHash) > 0 && verifyPassword(String(password), String(storedHash))) {
            JsonDocument respDoc;
            respDoc["success"] = true;
            JsonObject u = respDoc["user"].to<JsonObject>();
            u["user_id"] = user["id"] | 0;
            u["username"] = user["username"] | "";
            u["email"] = user["email"] | "";
            u["has_agreed_sos"] = (user["has_agreed_sos"] | 0) == 1;
            String response;
            serializeJson(respDoc, response);
            request->send(200, "application/json", response);
            return;
        }

        request->send(401, "application/json",
                      "{\"success\":false,\"error\":\"Invalid credentials\"}");
        return;
    }

    request->send(401, "application/json",
                  "{\"success\":false,\"error\":\"Invalid credentials\"}");
});
```

**Fix POST /change-password in `src/api_handlers.cpp`:**

Replace the password check and storage:

```cpp
// Old: strcmp(user["password"] | "", oldPw) != 0
// New:
const char* storedHash = user["password_hash"] | "";
if (!verifyPassword(String(oldPw), String(storedHash))) {
    request->send(401, "application/json",
                  "{\"success\":false,\"error\":\"Wrong old password\"}");
    return;
}
// Old: user["password"] = newPw;
// New:
user["password_hash"] = hashPassword(String(newPw));
```

---

## Issue 2: SYNC_BACK (Node → Admin)

The admin now handles `SYNC_BACK`, `SC`, and `SYNC_BACK_DONE` commands.
This lets the node send its local data back to the admin for merging.

**Add `sendSyncBack()` in `src/node_client.cpp`:**

```cpp
static void sendSyncBackPart(const char* partName, const char* sdFile) {
    JsonDocument fileDoc;
    readJsonFile(sdFile, fileDoc);
    JsonArray arr = fileDoc.is<JsonArray>() ? fileDoc.as<JsonArray>()
                                            : fileDoc.to<JsonArray>();
    int sent = 0;
    for (JsonVariant item : arr) {
        JsonObject rec = item.as<JsonObject>();

        JsonDocument msg;
        msg["cmd"] = "SYNC_BACK";
        msg["node_id"] = NODE_ID;
        msg["part"] = partName;
        msg["seq"] = sent;
        JsonObject d = msg["d"].to<JsonObject>();
        for (JsonPair kv : rec) {
            d[kv.key()] = kv.value();
        }

        String json;
        serializeJson(msg, json);

        if ((int)json.length() <= 72) {
            // Fits in broadcast
            xbeeSendBroadcast(json.c_str(), json.length());
        } else {
            // Too large for broadcast — send just id, rest as SC
            JsonDocument skelMsg;
            skelMsg["cmd"] = "SYNC_BACK";
            skelMsg["node_id"] = NODE_ID;
            skelMsg["part"] = partName;
            skelMsg["seq"] = sent;
            JsonObject skelD = skelMsg["d"].to<JsonObject>();
            // Copy only non-string fields (id, ints, bools)
            for (JsonPair kv : rec) {
                if (kv.value().is<const char*>()) {
                    skelD[kv.key()] = "";
                } else {
                    skelD[kv.key()] = kv.value();
                }
            }
            String skelJson;
            serializeJson(skelMsg, skelJson);
            xbeeSendBroadcast(skelJson.c_str(), skelJson.length());
            delay(50);

            // Send string fields as SC
            for (JsonPair kv : rec) {
                if (!kv.value().is<const char*>()) continue;
                const char* val = kv.value().as<const char*>();
                if (strlen(val) == 0) continue;

                int offset = 0;
                int fullLen = strlen(val);
                while (offset < fullLen) {
                    int chunkSize = 40;  // Conservative for broadcast
                    int end = (offset + chunkSize < fullLen)
                              ? offset + chunkSize : fullLen;

                    JsonDocument sc;
                    sc["cmd"] = "SC";
                    sc["p"] = partName;
                    sc["s"] = sent;
                    sc["k"] = kv.key().c_str();
                    sc["v"] = String(val).substring(offset, end);
                    String scJson;
                    serializeJson(sc, scJson);
                    if ((int)scJson.length() <= 72) {
                        xbeeSendBroadcast(scJson.c_str(), scJson.length());
                    }
                    delay(50);
                    offset = end;
                }
            }
        }

        sent++;
        delay(50);
    }

    // Count message
    JsonDocument countMsg;
    countMsg["cmd"] = "SYNC_BACK";
    countMsg["node_id"] = NODE_ID;
    countMsg["part"] = partName;
    countMsg["n"] = sent;
    String countJson;
    serializeJson(countMsg, countJson);
    xbeeSendBroadcast(countJson.c_str(), countJson.length());
    delay(50);
}

static void sendSyncBack() {
    // Only sync parts that the node generates locally
    sendSyncBackPart("chat_messages", SD_DMS_FILE);
    sendSyncBackPart("conversations", SD_CONVOS_FILE);

    JsonDocument done;
    done["cmd"] = "SYNC_BACK_DONE";
    done["node_id"] = NODE_ID;
    String doneJson;
    serializeJson(done, doneJson);
    xbeeSendBroadcast(doneJson.c_str(), doneJson.length());
}
```

**Add `/api/trigger/sync-back` endpoint in `src/api_handlers.cpp`:**

```cpp
server.on("/api/trigger/sync-back", HTTP_POST, [](AsyncWebServerRequest* request) {
    sendSyncBack();
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"SYNC_BACK sent\"}");
});
```

**Make `sendSyncBack()` accessible:**
Either move it to a public function or declare it in node_client.h:
```cpp
void nodeClientTriggerSyncBack();
```

---

## Issue 3: Oversized Announcements

The admin now handles oversized records differently. Instead of skipping records
whose base exceeds 240 bytes, it sends a skeleton with only numeric fields plus
empty strings, then sends ALL string fields as SC continuation messages.

**No node-side changes needed** — the existing SC handler already works.
Just make sure `handleSyncContinuation()` is implemented per Change 5 above.

---

## Quick Reference: All Commands

| Command | Direction | Purpose |
|---------|-----------|---------|
| PING | Admin→All | Discovery (broadcast, every 10s) |
| PONG | Node→Admin | Response to PING (broadcast) |
| REGISTER | Node→Admin | Register with admin (broadcast) |
| REGISTER_ACK | Admin→Node | Confirm registration (unicast) |
| HEARTBEAT | Node→Admin | Periodic status (broadcast, 30s) |
| SYNC_REQUEST | Node→Admin | Request data sync (broadcast) |
| SYNC_DATA | Admin→Node | One record at a time (unicast) |
| SC | Admin→Node | Continuation chunk (unicast) |
| SYNC_DONE | Admin→Node | End of sync (unicast) |
| SYNC_BACK | Node→Admin | Node sends data to admin (broadcast) |
| SYNC_BACK_DONE | Node→Admin | End of sync-back (broadcast) |
| GET_STATS | Admin→Node | Request node stats (unicast) |
| STATS_RESPONSE | Node→Admin | Stats reply (broadcast) |
| BROADCAST_MSG | Admin→Node | Push announcement (unicast) |
| SOS_ALERT | Node→Admin | Emergency alert (broadcast) |
| RELAY_CHAT_MSG | Node→Admin | Chat message relay (broadcast) |
| CHANGE_PASSWORD | Node→Admin | Password change relay (broadcast) |

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