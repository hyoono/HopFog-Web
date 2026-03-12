# HopFog-Node — Complete Fix Guide

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
> Branch: `copilot/setup-and-xbee-driver`
>
> **This supersedes ALL previous NODE_CHANGES documents.**

---

## Context

The admin and node communicate via XBee. Data syncs from admin→node.
The mobile app (Android/Kotlin) connects to the node's WiFi AP and calls
REST endpoints. The app has strict Kotlin data class expectations.

**Admin conversation format (synced to node):**
```json
{"id":1, "user1_id":1, "user2_id":2, "is_sos":false, "created_at":"12345"}
```

**Admin announcement format (synced to node) — NEW:**
The admin transforms broadcasts to mobile-friendly format before syncing:
```json
{"id":1, "title":"Emergency Notice", "message":"Body text...", "created_at":"12345"}
```
The node's `/announcements` endpoint can serve this directly — no transformation needed.

**XBee limits:** Broadcast ≤72 bytes, Unicast ≤240 bytes.

**COMPACT SYNC FORMAT:** The admin now sends SYNC_DATA, SC, and SYNC_DONE
using compact JSON keys to save ~30 bytes per message:

| Old Key | New Key | Example |
|---------|---------|---------|
| `"cmd":"SYNC_DATA"` | `"c":"SD"` | — |
| `"cmd":"SC"` | `"c":"SC"` | — |
| `"cmd":"SYNC_DONE"` | `"c":"DONE"` | — |
| `"node_id":"node-01"` | `"n":"node-01"` | — |
| `"part":"users"` | `"p":"users"` | — |
| `"seq":0` | `"s":0` | — |
| `"d":{record}` | `"d":{record}` | unchanged |
| `"n":3` (count) | `"n2":3` | ⚠️ `"n"` is now node_id! |

**Old format (no longer sent):**
```json
{"cmd":"SYNC_DATA","node_id":"node-01","part":"users","seq":0,"d":{...}}
{"cmd":"SYNC_DATA","node_id":"node-01","part":"users","n":3}
{"cmd":"SC","p":"users","s":0,"k":"body","v":"...text..."}
{"cmd":"SYNC_DONE","node_id":"node-01"}
```

**New compact format:**
```json
{"c":"SD","n":"node-01","p":"users","s":0,"d":{...}}
{"c":"SD","n":"node-01","p":"users","n2":3}
{"c":"SC","p":"users","s":0,"k":"body","v":"...text..."}
{"c":"DONE","n":"node-01"}
```

Note: SC messages already used compact keys — no change there.

**ALWAYS FULL SYNC:** Every sync (manual and auto) sends ALL records.
The node's sync handler OVERWRITES files — incremental sync would wipe
previously-synced data. This is correct behavior: the admin is the
source of truth for users, announcements, conversations, etc.

---

## Fix 1: POST /create-chat Response Format (CRITICAL)

The Android app's Kotlin data class `SosChatResponse` expects:
```json
{"conversation_id": 1, "contact_name": "John"}
```

**Replace the entire `/create-chat` handler in `src/api_handlers.cpp`:**

```cpp
    // ── POST /create-chat ───────────────────────────────────────────
    server.on("/create-chat", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int u1 = reqDoc["user1_id"] | 0;
        int u2 = reqDoc["user2_id"] | 0;
        if (u1 <= 0 || u2 <= 0) {
            request->send(400, "application/json", "{\"error\":\"user1_id and user2_id required\"}");
            return;
        }

        // Find or create conversation
        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);
        JsonArray arr = convoDoc.is<JsonArray>() ? convoDoc.as<JsonArray>()
                                                  : convoDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : arr) {
            int cu1 = c["user1_id"] | 0;
            int cu2 = c["user2_id"] | 0;
            if ((cu1 == u1 && cu2 == u2) || (cu1 == u2 && cu2 == u1)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            // Create new conversation
            int maxId = 0;
            for (JsonObject c : arr) {
                int id = c["id"] | 0;
                if (id > maxId) maxId = id;
            }
            convoId = maxId + 1;
            JsonObject newConvo = arr.add<JsonObject>();
            newConvo["id"] = convoId;
            newConvo["user1_id"] = u1;
            newConvo["user2_id"] = u2;
            newConvo["is_sos"] = 0;
            newConvo["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convoDoc);
        }

        // Look up contact name
        int otherId = (u1 == reqDoc["user1_id"].as<int>()) ? u2 : u1;
        String contactName = "Unknown";
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        if (usersDoc.is<JsonArray>()) {
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == otherId) {
                    contactName = u["username"] | "Unknown";
                    break;
                }
            }
        }

        // Return format matching SosChatResponse data class
        String resp = "{\"conversation_id\":" + String(convoId) +
                      ",\"contact_name\":\"" + contactName + "\"}";
        request->send(200, "application/json", resp);
    });
```

---

## Fix 2: POST /sos Response Format (CRITICAL)

Same data class `SosChatResponse`. Must return `conversation_id` + `contact_name`.

**Replace the entire `/sos` handler:**

```cpp
    // ── POST /sos ───────────────────────────────────────────────────
    server.on("/sos", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Body handled in onBody
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        DeserializationError err = deserializeJson(reqDoc, data, len);
        if (err) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int userId = reqDoc["user_id"] | 0;
        if (userId <= 0) {
            request->send(400, "application/json", "{\"error\":\"user_id required\"}");
            return;
        }

        // Find admin user (user_id=1 or role=admin)
        int adminId = 1;
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        if (usersDoc.is<JsonArray>()) {
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                String role = u["role"] | "";
                if (role == "admin") {
                    adminId = u["id"] | 1;
                    break;
                }
            }
        }

        // Find or create SOS conversation with admin
        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);
        JsonArray convos = convoDoc.is<JsonArray>() ? convoDoc.as<JsonArray>()
                                                     : convoDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : convos) {
            int cu1 = c["user1_id"] | 0;
            int cu2 = c["user2_id"] | 0;
            if ((cu1 == userId && cu2 == adminId) || (cu1 == adminId && cu2 == userId)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            int maxId = 0;
            for (JsonObject c : convos) {
                int id = c["id"] | 0;
                if (id > maxId) maxId = id;
            }
            convoId = maxId + 1;
            JsonObject newConvo = convos.add<JsonObject>();
            newConvo["id"] = convoId;
            newConvo["user1_id"] = userId;
            newConvo["user2_id"] = adminId;
            newConvo["is_sos"] = 1;
            newConvo["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convoDoc);
        }

        // Return format matching SosChatResponse data class
        String resp = "{\"conversation_id\":" + String(convoId) +
                      ",\"contact_name\":\"Admin\"}";
        request->send(200, "application/json", resp);

        // Relay SOS alert to admin via XBee (compact format)
        // Get username for the alert
        String userName = "User";
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            if ((u["id"] | 0) == userId) {
                userName = u["username"] | "User";
                break;
            }
        }
        JsonDocument sosCmd;
        sosCmd["c"] = "SOS";
        sosCmd["n"] = NODE_ID;
        sosCmd["si"] = userId;
        sosCmd["sn"] = userName;
        sosCmd["t"] = "SOS Emergency";
        String json;
        serializeJson(sosCmd, json);
        xbeeSendBroadcast(json.c_str(), json.length());
    });
```

**IMPORTANT:** Uses compact SOS format: `{"c":"SOS","n":"node-01","si":4,"sn":"Kurt","t":"SOS Emergency"}`
— about 65 bytes, safely under the 72-byte broadcast limit.

---

## Fix 3: GET /conversations Response Format (CRITICAL)

The app expects `ChatConversation` data class:
```json
[{"conversation_id":1, "contact_name":"John", "last_message":"Hey", "timestamp":"12345"}]
```

Conversations use `user1_id`/`user2_id` format (NOT `participants` array).

**Replace the entire `/conversations` handler:**

```cpp
    server.on("/conversations", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        if (userId.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"user_id required\"}");
            return;
        }
        int uid = userId.toInt();

        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);
        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject c : convoDoc.as<JsonArray>()) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if (u1 != uid && u2 != uid) continue;

            int otherId = (u1 == uid) ? u2 : u1;
            String contactName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == otherId) {
                    contactName = u["username"] | "Unknown";
                    break;
                }
            }

            String lastMsg = "";
            String lastTs = "";
            for (JsonObject m : dmDoc.as<JsonArray>()) {
                if ((m["conversation_id"] | 0) == (c["id"] | 0)) {
                    lastMsg = m["message_text"] | "";
                    long ts = m["sent_at"] | 0L;
                    lastTs = String(ts);
                }
            }

            JsonObject entry = arr.add<JsonObject>();
            entry["conversation_id"] = c["id"];
            entry["contact_name"] = contactName;
            entry["last_message"] = lastMsg;
            entry["timestamp"] = lastTs;
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });
```

---

## Fix 4: GET /messages Response Format

The app expects `Message` data class:
```json
[{"message_id":1, "message_text":"Hey", "sent_at":"12345", "sender_id":1, "is_from_current_user":true, "sender_username":"John"}]
```

**Replace the entire `/messages` handler:**

```cpp
    server.on("/messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String convoId = request->hasParam("conversation_id")
                         ? request->getParam("conversation_id")->value() : "";
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        if (convoId.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"conversation_id required\"}");
            return;
        }
        int cid = convoId.toInt();
        int uid = userId.toInt();

        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            if ((m["conversation_id"] | 0) != cid) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            JsonObject entry = arr.add<JsonObject>();
            entry["message_id"] = m["id"];
            entry["message_text"] = m["message_text"] | "";
            entry["sent_at"] = String(m["sent_at"] | 0L);
            entry["sender_id"] = senderId;
            entry["is_from_current_user"] = (senderId == uid);
            entry["sender_username"] = senderName;
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });
```

---

## Fix 5: POST /send Response Format + Relay to Admin

The app expects `SendMessageResponse`:
```json
{"success":true, "message":"sent", "secondsRemaining":0}
```

Uses `sent_at` (not `created_at`). Must relay to admin via XBee.

**Replace the `/send` handler:**

```cpp
    server.on("/send", HTTP_POST, [](AsyncWebServerRequest* request) {
    }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total) {
        JsonDocument reqDoc;
        if (deserializeJson(reqDoc, data, len)) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        int convoId = reqDoc["conversation_id"] | 0;
        int senderId = reqDoc["sender_id"] | 0;
        const char* text = reqDoc["message_text"] | "";
        if (convoId <= 0 || senderId <= 0 || strlen(text) == 0) {
            request->send(400, "application/json", "{\"error\":\"Missing fields\"}");
            return;
        }

        // Save to SD
        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);
        JsonArray arr = dmDoc.is<JsonArray>() ? dmDoc.as<JsonArray>()
                                               : dmDoc.to<JsonArray>();

        int maxId = 0;
        for (JsonObject m : arr) {
            int id = m["id"] | 0;
            if (id > maxId) maxId = id;
        }

        JsonObject msg = arr.add<JsonObject>();
        msg["id"] = maxId + 1;
        msg["conversation_id"] = convoId;
        msg["sender_id"] = senderId;
        msg["message_text"] = text;
        msg["sent_at"] = (long)(millis() / 1000);
        writeJsonFile(SD_DMS_FILE, dmDoc);

        request->send(200, "application/json",
                      "{\"success\":true,\"message\":\"sent\",\"secondsRemaining\":0}");

        // Relay to admin via XBee (compact format: <72 bytes for broadcast)
        // Format: {"c":"RCM","n":"node-01","ci":1,"si":4,"t":"hello"}
        JsonDocument relayCmd;
        relayCmd["c"] = "RCM";
        relayCmd["n"] = NODE_ID;
        relayCmd["ci"] = convoId;
        relayCmd["si"] = senderId;
        relayCmd["t"] = text;
        String json;
        serializeJson(relayCmd, json);
        xbeeSendBroadcast(json.c_str(), json.length());
    });
```

**IMPORTANT:** The relay uses compact keys (`"c":"RCM"` instead of
`"cmd":"RELAY_CHAT_MSG"`) to fit within the 72-byte broadcast limit.
A typical message `{"c":"RCM","n":"node-01","ci":1,"si":4,"t":"hello"}`
is ~55 bytes — safely under the limit. The admin understands both formats.

If the message text is very long (>30 chars), it may exceed 72 bytes.
That's OK — the XBee will log "OVERSIZED" but the message is still saved
locally on the node. The next auto-sync (every 5 min) will replicate it.

---

## Fix 6: GET /new-messages

The app polls for new messages. Must return the `Message` format.

**Replace the `/new-messages` handler:**

```cpp
    server.on("/new-messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        int lastId = request->hasParam("last_id")
                     ? request->getParam("last_id")->value().toInt() : 0;
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        int uid = userId.toInt();

        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        // Find conversations this user is part of
        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            int mId = m["id"] | 0;
            if (mId <= lastId) continue;

            int convoId = m["conversation_id"] | 0;
            // Check if user is part of this conversation
            bool inConvo = false;
            for (JsonObject c : convoDoc.as<JsonArray>()) {
                if ((c["id"] | 0) == convoId) {
                    int u1 = c["user1_id"] | 0;
                    int u2 = c["user2_id"] | 0;
                    if (u1 == uid || u2 == uid) inConvo = true;
                    break;
                }
            }
            if (!inConvo) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            JsonObject entry = arr.add<JsonObject>();
            entry["message_id"] = mId;
            entry["message_text"] = m["message_text"] | "";
            entry["sent_at"] = String(m["sent_at"] | 0L);
            entry["sender_id"] = senderId;
            entry["is_from_current_user"] = (senderId == uid);
            entry["sender_username"] = senderName;
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });
```

---

## Fix 7: Handle Compact SYNC_DATA Format

The admin now sends sync messages with compact keys. Update `nodeClientHandleCommand`
in `src/node_client.cpp` to handle both old and new format:

**Replace the command dispatch section in `nodeClientHandleCommand`:**

```cpp
bool nodeClientHandleCommand(const char* payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;

    // Check for compact format first: "c" key
    const char* compactCmd = doc["c"] | (const char*)nullptr;
    const char* legacyCmd  = doc["cmd"] | (const char*)nullptr;

    if (compactCmd) {
        // ── Compact format from admin sync ──
        if (strcmp(compactCmd, "SD") == 0) {
            // SYNC_DATA in compact format
            handleCompactSyncData(doc);
            return true;
        } else if (strcmp(compactCmd, "SC") == 0) {
            handleSyncContinuation(doc);
            return true;
        } else if (strcmp(compactCmd, "DONE") == 0) {
            handleSyncDone();
            return true;
        }
    }

    if (!legacyCmd) return false;  // No command found

    // ── Legacy format ──
    if (strcmp(legacyCmd, "REGISTER_ACK") == 0) {
        handleRegisterAck();
    } else if (strcmp(legacyCmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(legacyCmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);          // Legacy SYNC_DATA (backward compat)
    } else if (strcmp(legacyCmd, "SC") == 0) {
        handleSyncContinuation(doc);  // SC already uses compact keys
    } else if (strcmp(legacyCmd, "SYNC_DONE") == 0) {
        handleSyncDone();
    } else if (strcmp(legacyCmd, "PING") == 0) {
        JsonDocument pong;
        pong["cmd"] = "PONG";
        sendCommand(pong);
    } else if (strcmp(legacyCmd, "BROADCAST_MSG") == 0) {
        handleBroadcastMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "RELAY_CHAT_MSG") == 0) {
        handleRelayChatMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "SOS_ALERT") == 0) {
        handleSosAlert(doc["params"].as<JsonObject>());
    } else if (strcmp(legacyCmd, "GET_STATS") == 0) {
        handleGetStats();
    } else {
        return false;
    }
    return true;
}
```

**Add the new `handleCompactSyncData` function:**

```cpp
static void handleCompactSyncData(JsonDocument& doc) {
    // Compact format: "c"="SD", "n"=node_id, "p"=part, "s"=seq, "d"=record
    // Count message: "c"="SD", "n"=node_id, "p"=part, "n2"=count
    const char* part = doc["p"] | "";
    if (strlen(part) == 0) return;

    // Count message: "n2" = number of records for this part
    if (doc["n2"].is<int>()) {
        // Write accumulated buffer to SD
        if (strcmp(part, "users") == 0) {
            writeJsonFile(SD_USERS_FILE, syncUsers);
        } else if (strcmp(part, "announcements") == 0) {
            writeJsonFile(SD_ANNOUNCE_FILE, syncAnnouncements);
        } else if (strcmp(part, "conversations") == 0) {
            writeJsonFile(SD_CONVOS_FILE, syncConversations);
        } else if (strcmp(part, "chat_messages") == 0) {
            writeJsonFile(SD_DMS_FILE, syncChatMessages);
        } else if (strcmp(part, "fog_nodes") == 0) {
            writeJsonFile(SD_FOG_FILE, syncFogNodes);
        }
        return;
    }

    // Single record: "d" = record object
    if (doc["d"].is<JsonObject>()) {
        JsonObject record = doc["d"].as<JsonObject>();
        JsonDocument* buf = getSyncBuffer(part);
        if (buf) {
            buf->as<JsonArray>().add(record);
        }
    }
}
```

---

## Fix 8: Handle RELAY_CHAT_MSG (Live Chat Relay)

When the admin relays a chat message (user on admin network sends to user
on node network), the node must save it to SD so the recipient can see it.

**Add this function to `src/node_client.cpp`:**

```cpp
static void handleRelayChatMsg(JsonObject params) {
    int convId = params["conversation_id"] | 0;
    int senderId = params["sender_id"] | 0;
    const char* text = params["message_text"] | "";
    if (convId <= 0 || senderId <= 0 || strlen(text) == 0) return;

    JsonDocument doc;
    readJsonFile(SD_DMS_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int maxId = 0;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id > maxId) maxId = id;
    }

    JsonObject msg = arr.add<JsonObject>();
    msg["id"] = maxId + 1;
    msg["conversation_id"] = convId;
    msg["sender_id"] = senderId;
    msg["message_text"] = text;
    msg["sent_at"] = (long)(millis() / 1000);

    writeJsonFile(SD_DMS_FILE, doc);
}
```

---

## Fix 9: Handle SOS_ALERT

When the admin sends an SOS alert, the node stores it as an announcement.

**Add this function:**

```cpp
static void handleSosAlert(JsonObject params) {
    int userId = params["user_id"] | 0;
    const char* username = params["username"] | "User";

    JsonDocument doc;
    readJsonFile(SD_ANNOUNCE_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int maxId = 0;
    for (JsonObject a : arr) {
        int id = a["id"] | 0;
        if (id > maxId) maxId = id;
    }

    JsonObject ann = arr.add<JsonObject>();
    ann["id"] = maxId + 1;
    ann["title"] = "SOS Alert from " + String(username);
    ann["message"] = "Emergency SOS alert";
    ann["created_at"] = String((long)(millis() / 1000));

    writeJsonFile(SD_ANNOUNCE_FILE, doc);
}
```

---

## Fix 10: WiFi Stability

Add max TX power and disable WiFi power saving in `src/main.cpp` setup():

```cpp
#include <esp_wifi.h>

// After WiFi.softAP():
WiFi.setTxPower(WIFI_POWER_19_5dBm);
esp_wifi_set_ps(WIFI_PS_NONE);
```

---

## Fix 11: Captive Portal Detection

Add captive portal detection endpoints so phones don't show login popup.
**Add these BEFORE the `server.onNotFound(...)` call:**

```cpp
    // Android captive portal detection
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    // iOS/macOS captive portal detection
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    // Windows captive portal detection
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });
    // Firefox captive portal detection
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "success");
    });
```

**Remove any domain-based redirect in `server.onNotFound()`** — it interferes
with captive portal detection. The onNotFound should serve index.html from SD:

```cpp
    server.onNotFound([](AsyncWebServerRequest* request) {
        // Serve index.html for any unknown path (SPA behavior)
        request->send(SD, "/www/index.html", "text/html");
    });
```

---

## Fix 12: GET /announcements — Already Correct

The admin now syncs announcements in the mobile-friendly format:
```json
{"id":1, "title":"Notice", "message":"Body text", "created_at":"12345"}
```

The node's existing `sendJsonFileResponse(request, SD_ANNOUNCE_FILE)` will
serve this correctly. The mobile app's `Announcement` data class expects:
```kotlin
@Serializable data class Announcement(
    val id: Int,
    val title: String,
    val message: String,
    @SerialName("created_at") val createdAt: String? = null
)
```

**No changes needed** for GET /announcements — it already works.

---

## Summary

| Fix | What | Why |
|-----|------|-----|
| 1 | POST /create-chat | Returns wrong format → app crashes |
| 2 | POST /sos + compact SOS relay | Returns wrong format + relay uses compact `{"c":"SOS"}` (<72B) |
| 3 | GET /conversations | Missing contact_name, last_message |
| 4 | GET /messages | Missing sender_username, is_from_current_user |
| 5 | POST /send + compact chat relay | Wrong field names + relay uses compact `{"c":"RCM"}` (<72B) |
| 6 | GET /new-messages | Wrong format + no convo membership check |
| 7 | Compact SYNC_DATA | Admin sends "c":"SD" instead of "cmd":"SYNC_DATA" |
| 8 | RELAY_CHAT_MSG | Live chat relay from admin not handled |
| 9 | SOS_ALERT | SOS alerts from admin not handled |
| 10 | WiFi stability | Max TX power + disable power save |
| 11 | Captive portal | Phones show login popup without detection endpoints |
| 12 | GET /announcements | Already correct (no changes needed) |
| 13 | Battery monitoring (INA219) | Report battery level to admin via HEARTBEAT |
| 14 | LED status indicators | RGB LED for connection and battery status |
| 15 | Sync watchdog | Abort sync if stuck for 30 seconds |

---

## Fix 13: Battery Monitoring (INA219)

**What:** Add INA219 battery sensor support. Report battery data in HEARTBEAT messages.

**Why:** The admin dashboard shows battery status for both admin and nodes. Without this, the node battery panel shows "N/A".

### platformio.ini
Add to `lib_deps`:
```ini
adafruit/Adafruit INA219@^1.2.1
```

### Node HEARTBEAT format — add battery fields
When the node sends HEARTBEAT, include battery data:
```json
{"cmd":"HEARTBEAT","node_id":"node-01","free_heap":180000,"uptime":3600,"bat_v":3.85,"bat_pct":72,"bat_s":"normal"}
```

Where:
- `bat_v` = voltage in volts (float)
- `bat_pct` = percentage (0-100)
- `bat_s` = status string: "full", "normal", "low", "critical", "charging", "unknown"

### Battery code
Copy `include/battery.h` and `src/battery.cpp` from the admin repo — they are identical and work on both admin and node.

### I2C pins for ESP32-CAM
```cpp
Wire.begin(14, 15);  // SDA=GPIO14, SCL=GPIO15
```

If INA219 is not connected, `batteryInit()` returns false and all reads return safe defaults. The dashboard shows "N/A".

---

## Fix 14: LED Status Indicators

**What:** Add RGB LED indicators for connection and battery status.

**Why:** Visual feedback for device status without needing WiFi/web dashboard.

### LED behavior
| Condition | LED Color |
|-----------|-----------|
| Device on, no admin connected | RED constant |
| Searching for admin (PING sent, no PONG received) | YELLOW pulsing |
| Admin connected (PONG received within 30s) | GREEN constant |
| Critically low battery (<5%) | RED quick pulse |
| Low battery (<15%) | YELLOW constant |
| Charging | ORANGE constant |
| Full battery | GREEN constant |

### Code
Copy `include/led_status.h` and `src/led_status.cpp` from the admin repo.

In node's `loop()`:
```cpp
static unsigned long lastLedMs = 0;
if (millis() - lastLedMs > 200) {
    lastLedMs = millis();
    ConnectionStatus conn;
    if (lastPongReceivedMs > 0 && millis() - lastPongReceivedMs < 30000) {
        conn = CONN_CONNECTED;
    } else if (lastPingSentMs > 0) {
        conn = CONN_SEARCHING;
    } else {
        conn = CONN_DISCONNECTED;
    }
    BatteryInfo bat = batteryRead();
    ledStatusUpdate(conn, bat.percentage, bat.status == BAT_CHARGING);
}
```

### Pin assignment
```
LED_R = GPIO 12  (external red LED)
LED_G = GPIO 16  (external green LED)  
LED_B = GPIO 33  (built-in status LED, active LOW)
```

---

## Fix 15: Sync Watchdog

**What:** Add a 30-second timeout to the sync state machine.

**Why:** If sync gets stuck (e.g., XBee failure mid-sync), the node should abort and return to normal operation instead of hanging forever.

In the node's sync receive handler, track `lastSyncRxMs`. If no SYNC_DATA message received for 30 seconds while expecting more data, reset the sync state:

```cpp
// In the sync receive loop
if (syncInProgress && millis() - lastSyncRxMs > 30000) {
    syncInProgress = false;
    // Write whatever data was accumulated so far
    // Log warning
}
```
