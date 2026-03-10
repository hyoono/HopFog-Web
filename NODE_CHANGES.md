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

**XBee limits:** Broadcast ≤72 bytes, Unicast ≤240 bytes.

---

## Fix 1: POST /create-chat Response Format (CRITICAL)

The Android app's Kotlin data class `SosChatResponse` expects:
```json
{"conversation_id": 1, "contact_name": "John"}
```

**Current node response (WRONG):**
```json
{"success": true, "conversation": {"id": 1, "participants": [1, 2]}}
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

        int user1Id = reqDoc["user1_id"] | 0;
        int user2Id = reqDoc["user2_id"] | 0;

        if (user1Id <= 0 || user2Id <= 0) {
            request->send(400, "application/json",
                          "{\"error\":\"user1_id and user2_id required\"}");
            return;
        }

        // Find existing or create new conversation
        JsonDocument convDoc;
        readJsonFile(SD_CONVOS_FILE, convDoc);
        JsonArray convs = convDoc.is<JsonArray>()
                          ? convDoc.as<JsonArray>() : convDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : convs) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if ((u1 == user1Id && u2 == user2Id) ||
                (u1 == user2Id && u2 == user1Id)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            int newId = 1;
            for (JsonObject c : convs) {
                int id = c["id"] | 0;
                if (id >= newId) newId = id + 1;
            }
            JsonObject nc = convs.add<JsonObject>();
            nc["id"] = newId;
            nc["user1_id"] = user1Id;
            nc["user2_id"] = user2Id;
            nc["is_sos"] = false;
            nc["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convDoc);
            convoId = newId;
        }

        // Look up contact name
        int otherId = user2Id;
        String contactName = "Unknown";
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            if ((u["id"] | 0) == otherId) {
                contactName = u["username"] | "Unknown";
                break;
            }
        }

        // Return format matching admin's /create-chat
        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"] = contactName;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
```

---

## Fix 2: POST /sos Response Format (CRITICAL)

Same `SosChatResponse` data class. Current node returns `{"success":true,"message":"..."}`.

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

        // Find first admin user
        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);
        int adminId = 0;
        String adminName = "Admin";
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            String role = u["role"] | "";
            if (role == "admin" && (u["is_active"] | 1)) {
                adminId = u["id"] | 0;
                adminName = u["username"] | "Admin";
                break;
            }
        }

        if (adminId == 0) {
            request->send(500, "application/json",
                          "{\"error\":\"No admin user found\"}");
            return;
        }

        // Find or create SOS conversation
        JsonDocument convDoc;
        readJsonFile(SD_CONVOS_FILE, convDoc);
        JsonArray convs = convDoc.is<JsonArray>()
                          ? convDoc.as<JsonArray>() : convDoc.to<JsonArray>();

        int convoId = 0;
        for (JsonObject c : convs) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if ((u1 == userId && u2 == adminId) ||
                (u1 == adminId && u2 == userId)) {
                convoId = c["id"] | 0;
                break;
            }
        }

        if (convoId == 0) {
            int newId = 1;
            for (JsonObject c : convs) {
                int id = c["id"] | 0;
                if (id >= newId) newId = id + 1;
            }
            JsonObject nc = convs.add<JsonObject>();
            nc["id"] = newId;
            nc["user1_id"] = userId;
            nc["user2_id"] = adminId;
            nc["is_sos"] = true;
            nc["created_at"] = String((long)(millis() / 1000));
            writeJsonFile(SD_CONVOS_FILE, convDoc);
            convoId = newId;
        }

        // Relay SOS to admin via XBee
        JsonDocument relayDoc;
        relayDoc["cmd"] = "SOS_ALERT";
        JsonObject params = relayDoc["params"].to<JsonObject>();
        params["user_id"] = userId;
        params["conversation_id"] = convoId;
        relayDoc["node_id"] = NODE_ID;
        String json;
        serializeJson(relayDoc, json);
        xbeeSendBroadcast(json.c_str(), json.length());

        // Return format matching admin's /sos
        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"] = adminName;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
```

---

## Fix 3: GET /conversations Response Format (CRITICAL)

Admin syncs conversations with `user1_id`/`user2_id` format. The node's
current handler filters by `participants` array — which doesn't exist in
synced data! Result: all synced conversations invisible.

The admin's response format is:
```json
[
  {"conversation_id":1, "contact_name":"John", "last_message":"Hello", "timestamp":"12345"},
  {"conversation_id":2, "contact_name":"Admin", "last_message":null, "timestamp":null}
]
```

**Replace the entire `GET /conversations` handler:**

```cpp
    // ── GET /conversations ──────────────────────────────────────────
    server.on("/conversations", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";

        if (userId.length() == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"user_id required\"}");
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

            // Find last message
            int convoId = c["id"] | 0;
            String lastMsg = "";
            String lastTs = "";
            unsigned long latestTime = 0;
            for (JsonObject m : dmDoc.as<JsonArray>()) {
                if ((m["conversation_id"] | 0) != convoId) continue;
                unsigned long ts = m["sent_at"] | m["created_at"].as<unsigned long>();
                if (ts >= latestTime) {
                    latestTime = ts;
                    lastMsg = m["message_text"] | "";
                    lastTs = String(ts);
                }
            }

            JsonObject o = arr.add<JsonObject>();
            o["conversation_id"] = convoId;
            o["contact_name"] = contactName;
            o["last_message"] = lastMsg.length() > 0 ? lastMsg : JsonVariant();
            o["timestamp"] = lastTs.length() > 0 ? lastTs : JsonVariant();
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
```

---

## Fix 4: WiFi / Captive Portal (Match Admin)

The node's `web_server.cpp` has a captive portal domain redirect in
`onNotFound()` that triggers the phone's captive portal browser. The admin
does NOT do this. Also the admin has captive portal detection handlers
that prevent the phone from showing the login page.

**Replace the entire `setupWebServer()` in `src/web_server.cpp`:**

```cpp
#include "web_server.h"
#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

extern void registerApiHandlers(AsyncWebServer& server);

void setupWebServer(AsyncWebServer& server) {
    // CORS headers for mobile app
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                         "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                         "Content-Type, Authorization");

    // ── Captive portal detection (prevents login popup) ─────────────
    // Android
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });

    // Apple / iOS
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });
    server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });

    // Windows NCSI
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });

    // Firefox
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html",
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
    });
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "success\n");
    });

    // Register API endpoints
    registerApiHandlers(server);

    // 404 — NO captive portal redirect! Just return 404.
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
}
```

---

## Fix 5: GET /messages Format (Match Admin)

The admin returns messages with these fields. The node should match:

```cpp
    // ── GET /messages ───────────────────────────────────────────────
    server.on("/messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String convId = request->hasParam("conversation_id")
                        ? request->getParam("conversation_id")->value() : "";
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";

        if (convId.length() == 0 || userId.length() == 0) {
            request->send(400, "application/json",
                          "{\"error\":\"conversation_id and user_id required\"}");
            return;
        }

        int cid = convId.toInt();
        int uid = userId.toInt();

        JsonDocument doc;
        readJsonFile(SD_DMS_FILE, doc);

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : doc.as<JsonArray>()) {
            if ((m["conversation_id"] | 0) != cid) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            // Use sent_at or created_at (admin uses sent_at, node uses created_at)
            unsigned long ts = m["sent_at"] | 0UL;
            if (ts == 0 && m["created_at"].is<const char*>()) {
                ts = String(m["created_at"].as<const char*>()).toInt();
            } else if (ts == 0) {
                ts = m["created_at"] | 0UL;
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"] = m["id"];
            o["message_text"] = m["message_text"];
            o["sent_at"] = String(ts);
            o["sender_id"] = senderId;
            o["is_from_current_user"] = (senderId == uid);
            o["sender_username"] = senderName;
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
```

---

## Fix 6: GET /new-messages Format (Match Admin)

The admin's `/new-messages` returns the same format as `/messages` plus
checking conversation membership. Update the node to match:

```cpp
    // ── GET /new-messages ───────────────────────────────────────────
    server.on("/new-messages", HTTP_GET, [](AsyncWebServerRequest* request) {
        String userId = request->hasParam("user_id")
                        ? request->getParam("user_id")->value() : "";
        String lastIdStr = request->hasParam("last_id")
                           ? request->getParam("last_id")->value() : "0";

        int uid = userId.toInt();
        int lastId = lastIdStr.toInt();

        JsonDocument dmDoc;
        readJsonFile(SD_DMS_FILE, dmDoc);

        JsonDocument convoDoc;
        readJsonFile(SD_CONVOS_FILE, convoDoc);

        JsonDocument usersDoc;
        readJsonFile(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            if ((m["id"] | 0) <= lastId) continue;

            int cid = m["conversation_id"] | 0;
            bool userInConvo = false;
            for (JsonObject c : convoDoc.as<JsonArray>()) {
                if ((c["id"] | 0) != cid) continue;
                int u1 = c["user1_id"] | 0;
                int u2 = c["user2_id"] | 0;
                if (u1 == uid || u2 == uid) userInConvo = true;
                break;
            }
            if (uid > 0 && !userInConvo) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            unsigned long ts = m["sent_at"] | 0UL;
            if (ts == 0 && m["created_at"].is<const char*>()) {
                ts = String(m["created_at"].as<const char*>()).toInt();
            } else if (ts == 0) {
                ts = m["created_at"] | 0UL;
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"] = m["id"];
            o["message_text"] = m["message_text"];
            o["sent_at"] = String(ts);
            o["sender_id"] = senderId;
            o["is_from_current_user"] = (senderId == uid);
            o["sender_username"] = senderName;
        }

        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
```

---

## Fix 7: POST /send — Store message with sent_at (not created_at)

Admin uses `sent_at` field for message timestamps. Node currently uses
`created_at`. Update to use `sent_at` for consistency:

In the `/send` handler, change the message creation:
```cpp
        newMsg["sent_at"] = (long)(millis() / 1000);
```
Instead of:
```cpp
        newMsg["created_at"] = String((long)(millis() / 1000));
```

---

## Checklist

- [ ] Fix 1: Replace `/create-chat` handler → return `conversation_id` + `contact_name`
- [ ] Fix 2: Replace `/sos` handler → return `conversation_id` + `contact_name`
- [ ] Fix 3: Replace `GET /conversations` → use `user1_id`/`user2_id`, return expected format
- [ ] Fix 4: Replace `web_server.cpp` → captive portal detection, no domain redirect
- [ ] Fix 5: Replace `GET /messages` → match admin format
- [ ] Fix 6: Replace `GET /new-messages` → match admin format
- [ ] Fix 7: Fix `/send` → use `sent_at` not `created_at`
- [ ] Build and verify

---

## Important: Admin Now Uses Unicast for All Messages

The admin now sends `RELAY_CHAT_MSG`, `BROADCAST_MSG`, `SOS_ALERT`, and `GET_STATS`
via **unicast to each registered node** instead of broadcast. This means:

1. Messages up to **240 bytes** now work (was limited to 72 bytes broadcast)
2. The node receives the exact same JSON format — no changes needed on the RX side
3. If the node wants to forward messages to OTHER nodes, it should also use unicast
4. The admin falls back to broadcast if no nodes are registered yet
