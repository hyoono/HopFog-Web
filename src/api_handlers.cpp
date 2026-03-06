/*
 * api_handlers.cpp — REST API endpoints (mirrors original FastAPI routes)
 */

#include "api_handlers.h"
#include "sd_storage.h"
#include "auth.h"
#include "config.h"
#include "xbee_comm.h"
#include "node_protocol.h"

#include <ArduinoJson.h>
#ifdef USE_SD_MMC
  #include <SD_MMC.h>
#else
  #include <SD.h>
#endif
#include <time.h>
#include <sys/time.h>

static unsigned long currentEpoch() {
    time_t now;
    time(&now);
    return (unsigned long)now;
}

// ── Helper: authenticate request from cookie ────────────────────────

static int authenticateRequest(AsyncWebServerRequest *request) {
    if (!request->hasHeader("Cookie")) return -1;
    String cookie = request->header("Cookie");
    String token  = extractTokenFromCookie(cookie);
    return validateToken(token);
}

static void sendJsonError(AsyncWebServerRequest *request, int code, const char *msg) {
    JsonDocument doc;
    doc["error"] = msg;
    String out;
    serializeJson(doc, out);
    request->send(code, "application/json", out);
}

// ── Helper: JSON body handler for POST requests ─────────────────────
// Stores raw body in request->_tempObject so the main handler can
// parse it as JSON.  The main handler MUST call getJsonBody() to free
// the allocated memory, even if it discards the result.

static void jsonBodyHandler(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) {
    if (total > MAX_JSON_BODY) return;
    if (index == 0) {
        request->_tempObject = malloc(total + 1);
        if (!request->_tempObject) return;
    }
    if (request->_tempObject) {
        memcpy((uint8_t*)(request->_tempObject) + index, data, len);
        if (index + len == total) {
            ((char*)(request->_tempObject))[total] = '\0';
        }
    }
}

// Parse JSON body from _tempObject into doc. Returns true if valid JSON.
static bool getJsonBody(AsyncWebServerRequest *request, JsonDocument &doc) {
    if (!request->_tempObject) return false;
    DeserializationError err = deserializeJson(doc, (char*)(request->_tempObject));
    free(request->_tempObject);
    request->_tempObject = nullptr;
    return !err;
}

// Get a string param from JSON body (first) or form data (fallback).
static String getParam(AsyncWebServerRequest *request, JsonDocument &json,
                       const char *name, const char *defVal = "") {
    if (!json.isNull() && json[name].is<const char*>()) {
        return json[name].as<String>();
    }
    if (request->hasParam(name, true)) {
        return request->getParam(name, true)->value();
    }
    return String(defVal);
}

// Get an int param from JSON body (first) or form data (fallback).
static int getParamInt(AsyncWebServerRequest *request, JsonDocument &json,
                       const char *name, int defVal = 0) {
    if (!json.isNull() && json[name].is<JsonVariant>()) {
        return json[name].as<int>();
    }
    if (request->hasParam(name, true)) {
        return request->getParam(name, true)->value().toInt();
    }
    return defVal;
}

// ── Register routes ────────────────────────────────────────────────

void registerApiRoutes(AsyncWebServer &server) {

    // ╭───────────────────────────────────────────────────────────────╮
    // │  AUTH: POST /login                                           │
    // │  Web admin: form data (email+password) → redirect            │
    // │  Mobile app: JSON (username+password) → JSON response        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Try to parse JSON body (mobile app sends application/json)
        JsonDocument jsonBody;
        bool isJson = getJsonBody(request, jsonBody);

        if (isJson) {
            // ── Mobile login flow ────────────────────────────────────
            String username = jsonBody["username"] | "";
            String password = jsonBody["password"] | "";

            if (username.isEmpty() || password.isEmpty()) {
                JsonDocument r; r["success"] = false; r["message"] = "Missing username or password";
                String out; serializeJson(r, out);
                request->send(400, "application/json", out);
                return;
            }

            JsonDocument userDoc = getUserByUsername(username.c_str());
            if (userDoc.isNull() || userDoc.size() == 0) {
                JsonDocument r; r["success"] = false; r["message"] = "Invalid credentials";
                String out; serializeJson(r, out);
                request->send(401, "application/json", out);
                return;
            }

            int    userId   = userDoc["id"] | 0;
            int    isActive = userDoc["is_active"] | 0;
            String storedHash = userDoc["password_hash"] | "";

            if (!isActive) {
                JsonDocument r; r["success"] = false; r["message"] = "Account is inactive";
                String out; serializeJson(r, out);
                request->send(403, "application/json", out);
                return;
            }
            if (!verifyPassword(password, storedHash)) {
                JsonDocument r; r["success"] = false; r["message"] = "Invalid credentials";
                String out; serializeJson(r, out);
                request->send(401, "application/json", out);
                return;
            }

            // Success — return JSON with user info (matching mobile app expectations)
            JsonDocument resp;
            resp["success"] = true;
            JsonObject u = resp["user"].to<JsonObject>();
            u["user_id"]        = userId;
            u["username"]       = userDoc["username"] | "";
            u["email"]          = userDoc["email"] | "";
            u["has_agreed_sos"] = (userDoc["has_agreed_sos"] | 0) ? true : false;
            String out; serializeJson(resp, out);
            request->send(200, "application/json", out);
            return;
        }

        // ── Web admin login flow (form data) ─────────────────────────
        if (!request->hasParam("email", true) || !request->hasParam("password", true)) {
            request->redirect("/?error=missing_fields");
            return;
        }
        String email    = request->getParam("email", true)->value();
        String password = request->getParam("password", true)->value();

        JsonDocument userDoc = getUserByEmail(email.c_str());
        if (userDoc.isNull() || userDoc.size() == 0) {
            request->redirect("/?error=invalid");
            return;
        }

        String storedHash = userDoc["password_hash"] | "";
        String role       = userDoc["role"] | "";
        int    userId     = userDoc["id"] | 0;
        int    isActive   = userDoc["is_active"] | 0;

        if (role != "admin") {
            request->redirect("/?error=not_admin");
            return;
        }
        if (!isActive) {
            request->redirect("/?error=inactive");
            return;
        }
        if (!verifyPassword(password, storedHash)) {
            request->redirect("/?error=invalid");
            return;
        }

        String token = createSessionToken(userId);
        AsyncWebServerResponse *resp = request->beginResponse(302);
        resp->addHeader("Location", "/dashboard");
        resp->addHeader("Set-Cookie", "access_token=Bearer " + token + "; HttpOnly; Path=/");
        request->send(resp);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  AUTH: POST /register                                        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("username", true) ||
            !request->hasParam("email", true) ||
            !request->hasParam("password", true)) {
            request->redirect("/register?error=missing_fields");
            return;
        }
        String username = request->getParam("username", true)->value();
        String email    = request->getParam("email", true)->value();
        String password = request->getParam("password", true)->value();

        if (userEmailExists(email.c_str())) {
            request->redirect("/register?error=email_taken");
            return;
        }
        if (userNameExists(username.c_str())) {
            request->redirect("/register?error=username_taken");
            return;
        }

        String hash = hashPassword(password);
        createUser(username.c_str(), email.c_str(), hash.c_str(), "admin", true);
        request->redirect("/?registered=true");
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  AUTH: POST /forgot-password                                 │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/forgot-password", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("email", true) || !request->hasParam("new_password", true)) {
            sendJsonError(request, 400, "Missing fields");
            return;
        }
        String email       = request->getParam("email", true)->value();
        String newPassword = request->getParam("new_password", true)->value();

        JsonDocument userDoc = getUserByEmail(email.c_str());
        if (userDoc.isNull() || userDoc.size() == 0) {
            JsonDocument r; r["success"] = false; r["message"] = "No account found.";
            String out; serializeJson(r, out);
            request->send(200, "application/json", out);
            return;
        }
        String role = userDoc["role"] | "";
        if (role != "admin") {
            JsonDocument r; r["success"] = false; r["message"] = "Mobile users must contact an admin.";
            String out; serializeJson(r, out);
            request->send(200, "application/json", out);
            return;
        }
        if (newPassword.length() < 6) {
            JsonDocument r; r["success"] = false; r["message"] = "Password must be >= 6 chars.";
            String out; serializeJson(r, out);
            request->send(200, "application/json", out);
            return;
        }

        int userId = userDoc["id"] | 0;
        String hash = hashPassword(newPassword);
        updateUserField(userId, "password_hash", hash.c_str());

        JsonDocument r; r["success"] = true; r["message"] = "Password reset successfully!";
        String out; serializeJson(r, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  DASHBOARD DATA: GET /api/dashboard                          │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument fogDoc;
        readJsonArray(SD_FOG_FILE, fogDoc);
        JsonArray fogArr = fogDoc.as<JsonArray>();
        int fogCount = fogArr.size();
        int activeCount = 0;
        int totalConnected = 0;
        float storageUsed = 0, storageTotal = 0;
        for (JsonObject d : fogArr) {
            if (strcmp(d["status"] | "", "active") == 0) activeCount++;
            totalConnected += d["connected_users"] | 0;
            // Simple numeric parse
            String su = d["storage_used"] | "0";
            String st = d["storage_total"] | "0";
            su.replace("GB", ""); su.replace("MB", ""); su.trim();
            st.replace("GB", ""); st.replace("MB", ""); st.trim();
            storageUsed  += su.toFloat();
            storageTotal += st.toFloat();
        }

        JsonDocument bcDoc;
        readJsonArray(SD_BCASTS_FILE, bcDoc);
        int totalSosAlerts = 0;
        for (JsonObject b : bcDoc.as<JsonArray>()) {
            const char* mt = b["msg_type"] | "";
            if (strcmp(mt, "alert") == 0 || strcmp(mt, "sos") == 0)
                totalSosAlerts++;
        }

        JsonDocument userDoc = getUserById(uid);

        JsonDocument resp;
        resp["fog_nodes_count"]  = fogCount;
        resp["active_fog_nodes"] = activeCount;
        resp["inactive_fog_nodes"] = fogCount - activeCount;
        resp["people_connected"]   = totalConnected;
        resp["total_sos_alerts"]   = totalSosAlerts;
        resp["storage_display"]    = (storageTotal > 0)
            ? String(storageUsed, 1) + "GB / " + String(storageTotal, 1) + "GB"
            : "N/A";
        JsonObject cu = resp["current_user"].to<JsonObject>();
        cu["id"]         = userDoc["id"];
        cu["username"]   = userDoc["username"];
        cu["email"]      = userDoc["email"];
        cu["role"]       = userDoc["role"];
        cu["is_active"]  = userDoc["is_active"];
        cu["created_at"] = userDoc["created_at"];

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  USERS: GET /api/users                                       │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/users", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_USERS_FILE, doc);

        // Strip password hashes before sending
        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();
        for (JsonObject u : doc.as<JsonArray>()) {
            JsonObject o = arr.add<JsonObject>();
            o["id"]         = u["id"];
            o["username"]   = u["username"];
            o["email"]      = u["email"];
            o["role"]       = u["role"];
            o["is_active"]  = u["is_active"];
            o["created_at"] = u["created_at"];
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  USERS: POST /api/admin/create-mobile-user                   │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/admin/create-mobile-user", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        if (!request->hasParam("username", true) ||
            !request->hasParam("email", true) ||
            !request->hasParam("password", true)) {
            sendJsonError(request, 400, "Missing fields");
            return;
        }
        String username = request->getParam("username", true)->value();
        String email    = request->getParam("email", true)->value();
        String password = request->getParam("password", true)->value();

        if (userEmailExists(email.c_str())) {
            sendJsonError(request, 400, "Email already registered");
            return;
        }
        if (userNameExists(username.c_str())) {
            sendJsonError(request, 400, "Username already taken");
            return;
        }

        String hash = hashPassword(password);
        int newId = createUser(username.c_str(), email.c_str(), hash.c_str(), "mobile", true);

        JsonDocument resp;
        resp["message"] = "Mobile user created successfully";
        JsonObject u = resp["user"].to<JsonObject>();
        u["id"]       = newId;
        u["username"] = username;
        u["email"]    = email;
        u["role"]     = "mobile";

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  USERS: PUT /api/users/{id}/toggle-status                    │
    // ╰───────────────────────────────────────────────────────────────╯
    // ESPAsyncWebServer doesn't support path params natively;
    // we match a wildcard pattern and parse the id.
    server.on("^\\/api\\/users\\/(\\d+)\\/toggle-status$", HTTP_PUT,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int targetId = request->pathArg(0).toInt();
        if (targetId == uid) {
            sendJsonError(request, 400, "Cannot modify your own status");
            return;
        }
        JsonDocument target = getUserById(targetId);
        if (target.isNull()) {
            sendJsonError(request, 404, "User not found");
            return;
        }
        String role = target["role"] | "";
        if (role == "admin") {
            sendJsonError(request, 400, "Cannot modify admin user status");
            return;
        }

        toggleUserActive(targetId);

        JsonDocument resp;
        resp["message"] = "User status toggled";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MESSAGES: GET /api/messages                                  │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/messages", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_MSGS_FILE, doc);

        // Build user ID → name lookup map (O(m) once, not O(m) per message)
        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);
        JsonDocument userMap;
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            String key = String(u["id"] | 0);
            userMap[key] = u["username"] | "Unknown";
        }

        JsonDocument resp;
        JsonArray out = resp.to<JsonArray>();

        for (JsonObject msg : doc.as<JsonArray>()) {
            JsonObject m = out.add<JsonObject>();
            m["id"]         = msg["id"];
            m["body"]       = msg["body"];
            m["subject"]    = msg["subject"];
            m["created_at"] = msg["created_at"];

            String sKey = String(msg["sender_id"] | 0);
            m["from"] = userMap[sKey] | "Unknown";

            // Resolve recipient usernames
            JsonArray toArr = m["to"].to<JsonArray>();
            if (msg["recipients"].is<JsonArray>()) {
                for (JsonObject r : msg["recipients"].as<JsonArray>()) {
                    String rKey = String(r["user_id"] | 0);
                    toArr.add(userMap[rKey] | "Unknown");
                }
            }
        }

        String outStr;
        serializeJson(resp, outStr);
        request->send(200, "application/json", outStr);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MESSAGES: DELETE /api/messages/{id}                          │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/messages\\/(\\d+)$", HTTP_DELETE,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int msgId = request->pathArg(0).toInt();
        if (deleteMessage(msgId)) {
            JsonDocument r; r["message"] = "Message deleted";
            String out; serializeJson(r, out);
            request->send(200, "application/json", out);
        } else {
            sendJsonError(request, 404, "Message not found");
        }
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  FOG DEVICES: GET /api/fog-devices                           │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/fog-devices", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_FOG_FILE, doc);

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  FOG DEVICES: POST /api/fog-devices/register                 │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/fog-devices/register", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        if (!request->hasParam("device_name", true)) {
            sendJsonError(request, 400, "device_name required");
            return;
        }
        String name = request->getParam("device_name", true)->value();
        int id = registerFogDevice(name.c_str());

        JsonDocument resp;
        resp["message"]   = "Device registered";
        resp["device_id"] = id;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  FOG DEVICES: POST /api/fog-devices/{id}/disconnect          │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/fog-devices\\/(\\d+)\\/disconnect$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int devId = request->pathArg(0).toInt();
        disconnectFogDevice(devId);

        JsonDocument resp;
        resp["message"] = "Device disconnected";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  FOG DEVICES: POST /api/fog-devices/{id}/status              │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/fog-devices\\/(\\d+)\\/status$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int devId = request->pathArg(0).toInt();
        String status       = request->hasParam("status", true)        ? request->getParam("status", true)->value()        : "active";
        String storageUsed  = request->hasParam("storage_used", true)  ? request->getParam("storage_used", true)->value()  : "N/A";
        String storageTotal = request->hasParam("storage_total", true) ? request->getParam("storage_total", true)->value() : "N/A";
        int connUsers       = request->hasParam("connected_users", true) ? request->getParam("connected_users", true)->value().toInt() : 0;

        updateFogDeviceStatus(devId, status.c_str(), storageUsed.c_str(),
                              storageTotal.c_str(), connUsers);

        JsonDocument resp;
        resp["message"] = "Status updated";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  BROADCASTS: GET /api/broadcasts/{id}  (detail + events)     │
    // │  ⚠ Must be registered BEFORE non-regex /api/broadcasts       │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/broadcasts\\/(\\d+)$", HTTP_GET,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int bId = request->pathArg(0).toInt();

        // Find the broadcast
        JsonDocument bcastDoc;
        readJsonArray(SD_BCASTS_FILE, bcastDoc);
        bool found = false;
        JsonDocument resp;

        for (JsonObject b : bcastDoc.as<JsonArray>()) {
            if ((b["id"] | 0) == bId) {
                // Copy broadcast fields
                for (JsonPair kv : b) {
                    resp[kv.key()] = kv.value();
                }
                found = true;
                break;
            }
        }

        if (!found) { sendJsonError(request, 404, "Broadcast not found"); return; }

        // Add recipient status counts
        int total=0, queued=0, sent=0, delivered=0, readCount=0, failed=0;
        getRecipientStatusCounts(bId, total, queued, sent, delivered, readCount, failed);
        JsonObject sc = resp["status_counts"].to<JsonObject>();
        sc["total"]     = total;
        sc["queued"]    = queued;
        sc["sent"]      = sent;
        sc["delivered"] = delivered;
        sc["read"]      = readCount;
        sc["failed"]    = failed;

        // Add events
        JsonDocument evDoc;
        getBroadcastEvents(bId, evDoc);
        resp["events"] = evDoc.as<JsonArray>();

        String outStr;
        serializeJson(resp, outStr);
        request->send(200, "application/json", outStr);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  BROADCASTS: POST /api/broadcasts/{id}/mark_sent             │
    // │  ⚠ Must be registered BEFORE non-regex /api/broadcasts       │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/broadcasts\\/(\\d+)\\/mark_sent$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int bId = request->pathArg(0).toInt();

        // Validate broadcast exists and is in a sendable state
        JsonDocument bcastDoc;
        readJsonArray(SD_BCASTS_FILE, bcastDoc);
        bool found = false;
        String curStatus, subject, msgType, body;
        for (JsonObject b : bcastDoc.as<JsonArray>()) {
            if ((b["id"] | 0) == bId) {
                curStatus = b["status"] | "";
                subject   = b["subject"] | "";
                msgType   = b["msg_type"] | "";
                body      = b["body"] | "";
                found = true;
                break;
            }
        }
        if (!found) { sendJsonError(request, 404, "Broadcast not found"); return; }
        if (curStatus == "sent" || curStatus == "cancelled" || curStatus == "failed") {
            sendJsonError(request, 400, ("Cannot mark_sent: broadcast is already " + curStatus).c_str());
            return;
        }

        // Update broadcast status
        updateBroadcastStatus(bId, "sent");
        // Update all recipients to "sent" with sent_at timestamp
        updateRecipientsStatus(bId, "sent");
        // Create audit event
        addBroadcastEvent(bId, "marked_sent", "Manually marked as sent (simulation)");

        // ── Send via XBee S2C (JSON command for node protocol) ────────
        JsonDocument markCmd;
        markCmd["cmd"] = "BROADCAST_MSG";
        JsonObject markParams = markCmd["params"].to<JsonObject>();
        markParams["from"] = "admin";
        markParams["to"] = "all";
        markParams["message"] = body;
        markParams["subject"] = subject;
        markParams["msg_type"] = msgType;
        String markJson;
        serializeJson(markCmd, markJson);
        xbeeSendBroadcast(markJson.c_str(), markJson.length());

        // Log activity
        String logSubject = "[Sent] " + subject;
        logActivity(uid, logSubject.c_str(), body.c_str());

        JsonDocument resp;
        resp["message"] = "Marked as sent";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  BROADCASTS: POST /api/broadcasts/{id}/cancel                │
    // │  ⚠ Must be registered BEFORE non-regex /api/broadcasts       │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/broadcasts\\/(\\d+)\\/cancel$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int bId = request->pathArg(0).toInt();

        // Validate broadcast exists and is cancellable
        JsonDocument bcastDoc;
        readJsonArray(SD_BCASTS_FILE, bcastDoc);
        bool found = false;
        String curStatus;
        for (JsonObject b : bcastDoc.as<JsonArray>()) {
            if ((b["id"] | 0) == bId) {
                curStatus = b["status"] | "";
                found = true;
                break;
            }
        }
        if (!found) { sendJsonError(request, 404, "Broadcast not found"); return; }
        if (curStatus == "sent" || curStatus == "cancelled") {
            sendJsonError(request, 400, ("Cannot cancel: broadcast is already " + curStatus).c_str());
            return;
        }

        updateBroadcastStatus(bId, "cancelled");
        addBroadcastEvent(bId, "cancelled", "Broadcast cancelled by admin");

        // Log activity
        String logSubject = "[Cancelled] Broadcast #" + String(bId);
        logActivity(uid, logSubject.c_str(), "Broadcast cancelled by admin");

        JsonDocument resp;
        resp["message"] = "Broadcast cancelled";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  BROADCASTS: GET /api/broadcasts                             │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/broadcasts", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_BCASTS_FILE, doc);

        // Read ALL recipient status counts in one pass (avoids N+1 SD reads)
        JsonDocument countsMap;
        getAllRecipientStatusCounts(countsMap);

        // Enrich each broadcast with recipient status counts
        JsonDocument resp;
        JsonArray out = resp.to<JsonArray>();
        for (JsonObject b : doc.as<JsonArray>()) {
            JsonObject o = out.add<JsonObject>();
            for (JsonPair kv : b) {
                o[kv.key()] = kv.value();
            }
            String key = String(b["id"] | 0);
            JsonObject sc = o["status_counts"].to<JsonObject>();
            if (countsMap[key].is<JsonObject>()) {
                JsonObject src = countsMap[key].as<JsonObject>();
                sc["total"]     = src["total"]     | 0;
                sc["queued"]    = src["queued"]    | 0;
                sc["sent"]      = src["sent"]      | 0;
                sc["delivered"] = src["delivered"] | 0;
                sc["read"]      = src["read"]      | 0;
                sc["failed"]    = src["failed"]    | 0;
            } else {
                sc["total"]=0; sc["queued"]=0; sc["sent"]=0;
                sc["delivered"]=0; sc["read"]=0; sc["failed"]=0;
            }
        }

        String outStr;
        serializeJson(resp, outStr);
        request->send(200, "application/json", outStr);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  BROADCASTS: POST /api/broadcasts                            │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/broadcasts", HTTP_POST, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String msgType  = request->hasParam("msg_type", true)  ? request->getParam("msg_type", true)->value()  : "announcement";
        String severity = request->hasParam("severity", true)  ? request->getParam("severity", true)->value()  : "info";
        String audience = request->hasParam("audience", true)   ? request->getParam("audience", true)->value()  : "all_residents";
        String subject  = request->hasParam("subject", true)    ? request->getParam("subject", true)->value()   : "";
        String body     = request->hasParam("body", true)       ? request->getParam("body", true)->value()      : "";
        String status   = request->hasParam("status", true)     ? request->getParam("status", true)->value()    : "draft";
        int ttlHours    = request->hasParam("ttl_hours", true)  ? request->getParam("ttl_hours", true)->value().toInt() : 24;

        // Validate enums (match original Python)
        msgType.toLowerCase();
        severity.toLowerCase();
        status.toLowerCase();
        if (msgType != "announcement" && msgType != "alert" && msgType != "sos") msgType = "announcement";
        if (severity != "info" && severity != "warning" && severity != "critical") severity = "info";
        if (status != "draft" && status != "queued") status = "draft";

        // Clamp TTL: 1 hour minimum, 720 hours (30 days) maximum
        if (ttlHours < 1) ttlHours = 1;
        if (ttlHours > 720) ttlHours = 720;

        int priority = 10;
        if (msgType == "sos")   priority = 100;
        else if (msgType == "alert") priority = 50;

        int id = createBroadcast(uid, msgType.c_str(), severity.c_str(),
                                 audience.c_str(), subject.c_str(), body.c_str(),
                                 status.c_str(), priority, ttlHours);

        // Create recipient records for all active residents
        createRecipientsForBroadcast(id);

        // Create audit events
        addBroadcastEvent(id, "created", "Broadcast created");
        if (status == "queued") {
            addBroadcastEvent(id, "queued", "Broadcast queued for delivery");

            // ── ESP32 auto-dispatch: no background dispatcher exists, so
            //    immediately send via XBee and mark as "sent". ──────────
            // Send JSON-framed command (for HopFog-Node protocol)
            JsonDocument bcastCmd;
            bcastCmd["cmd"] = "BROADCAST_MSG";
            JsonObject bcastParams = bcastCmd["params"].to<JsonObject>();
            bcastParams["from"] = "admin";
            bcastParams["to"] = "all";
            bcastParams["message"] = body;
            bcastParams["subject"] = subject;
            bcastParams["msg_type"] = msgType;
            bcastParams["severity"] = severity;
            String bcastJson;
            serializeJson(bcastCmd, bcastJson);
            uint8_t frameId = xbeeSendBroadcast(bcastJson.c_str(), bcastJson.length());
            if (frameId > 0) {
                updateBroadcastStatus(id, "sent");
                updateRecipientsStatus(id, "sent");
                addBroadcastEvent(id, "marked_sent", "Auto-sent via XBee (no dispatcher)");
                status = "sent";
            } else {
                updateBroadcastStatus(id, "failed");
                addBroadcastEvent(id, "failed", "XBee transmission failed");
                status = "failed";
            }
        }

        // Log activity
        String logSubject = "[Broadcast] " + subject;
        logActivity(uid, logSubject.c_str(), body.c_str());

        JsonDocument resp;
        resp["message"]      = "Broadcast created";
        resp["broadcast_id"] = id;
        resp["status"]       = status;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  SETTINGS: POST /api/change-password                         │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/change-password", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String currentPw = request->hasParam("current_password", true) ? request->getParam("current_password", true)->value() : "";
        String newPw     = request->hasParam("new_password", true)     ? request->getParam("new_password", true)->value()     : "";
        String confirmPw = request->hasParam("confirm_password", true) ? request->getParam("confirm_password", true)->value() : "";

        if (newPw != confirmPw) {
            sendJsonError(request, 400, "New passwords do not match");
            return;
        }
        if (newPw.length() < 6) {
            sendJsonError(request, 400, "Password must be >= 6 characters");
            return;
        }

        JsonDocument userDoc = getUserById(uid);
        String storedHash = userDoc["password_hash"] | "";
        if (!verifyPassword(currentPw, storedHash)) {
            sendJsonError(request, 400, "Current password is incorrect");
            return;
        }

        String newHash = hashPassword(newPw);
        updateUserField(uid, "password_hash", newHash.c_str());

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "Password changed successfully!";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE LOGIN: POST /api/mobile/login                        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/mobile/login", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        if (!request->hasParam("email", true) || !request->hasParam("password", true)) {
            sendJsonError(request, 400, "Missing email or password");
            return;
        }
        String email    = request->getParam("email", true)->value();
        String password = request->getParam("password", true)->value();

        JsonDocument userDoc = getUserByEmail(email.c_str());
        if (userDoc.isNull() || userDoc.size() == 0) {
            sendJsonError(request, 401, "Invalid email or password");
            return;
        }

        String role     = userDoc["role"] | "";
        int    isActive = userDoc["is_active"] | 0;
        int    userId   = userDoc["id"] | 0;

        if (role != "mobile") {
            sendJsonError(request, 403, "Not authorized for mobile access");
            return;
        }
        if (!verifyPassword(password, userDoc["password_hash"] | "")) {
            sendJsonError(request, 401, "Invalid email or password");
            return;
        }
        if (!isActive) {
            sendJsonError(request, 403, "Account is inactive");
            return;
        }

        String token = createSessionToken(userId);

        JsonDocument resp;
        resp["access_token"] = token;
        resp["token_type"]   = "bearer";
        JsonObject u = resp["user"].to<JsonObject>();
        u["id"]       = userId;
        u["username"] = userDoc["username"] | "";
        u["email"]    = email;
        u["role"]     = role;

        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  RESIDENT→ADMIN MSGS: GET /api/resident-admin/messages       │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/resident-admin/messages", HTTP_GET,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_RES_MSG_FILE, doc);
        String out; serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  RESIDENT→ADMIN MSGS: POST /api/resident-admin/messages      │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/resident-admin/messages", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        String kind    = request->hasParam("kind", true)    ? request->getParam("kind", true)->value()    : "message";
        String subject = request->hasParam("subject", true) ? request->getParam("subject", true)->value() : "";
        String body    = request->hasParam("body", true)    ? request->getParam("body", true)->value()    : "";
        int senderId   = request->hasParam("sender_id", true) ? request->getParam("sender_id", true)->value().toInt() : 0;

        int priority = (kind == "sos_request") ? 90 : 30;

        JsonDocument doc;
        if (!readJsonArray(SD_RES_MSG_FILE, doc)) { doc.to<JsonArray>(); }
        JsonArray arr = doc.as<JsonArray>();
        int id = nextId(SD_RES_MSG_FILE);

        JsonObject msg = arr.add<JsonObject>();
        msg["id"]           = id;
        msg["sender_id"]    = senderId;
        msg["kind"]         = kind;
        msg["subject"]      = subject;
        msg["body"]         = body;
        msg["priority"]     = priority;
        msg["status"]       = "queued";
        msg["admin_action"] = "none";
        msg["created_at"]   = currentEpoch();

        writeJsonArray(SD_RES_MSG_FILE, doc);

        JsonDocument resp;
        resp["message"] = "Message created";
        resp["id"]      = id;
        String out; serializeJson(resp, out);
        request->send(201, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  SOS: POST /api/sos-requests/{id}/escalate                   │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/sos-requests\\/(\\d+)\\/escalate$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int reqId = request->pathArg(0).toInt();
        String escalateTo = request->hasParam("escalate_to", true)
            ? request->getParam("escalate_to", true)->value() : "sos";
        // Validate escalate_to
        escalateTo.toLowerCase();
        if (escalateTo != "sos" && escalateTo != "alert" && escalateTo != "announcement") {
            escalateTo = "sos";
        }

        // Read the SOS request
        JsonDocument resDoc;
        readJsonArray(SD_RES_MSG_FILE, resDoc);
        JsonObject found;
        bool exists = false;
        for (JsonObject m : resDoc.as<JsonArray>()) {
            if ((m["id"] | 0) == reqId) { found = m; exists = true; break; }
        }
        if (!exists) { sendJsonError(request, 404, "SOS request not found"); return; }

        // Create broadcast from the SOS request
        String severity = "info";
        if (escalateTo == "alert") severity = "warning";
        else if (escalateTo == "sos") severity = "critical";

        int priority = 10;
        if (escalateTo == "sos") priority = 100;
        else if (escalateTo == "alert") priority = 50;

        String subject = found["subject"] | "Resident SOS Request";
        String body    = found["body"] | "";

        int bId = createBroadcast(uid, escalateTo.c_str(), severity.c_str(),
                       "all_residents", subject.c_str(), body.c_str(),
                       "queued", priority, 2);  // 2-hour TTL for SOS

        // Create recipients and audit events
        createRecipientsForBroadcast(bId);
        String eventMsg = "Created from SOS request #" + String(reqId);
        addBroadcastEvent(bId, "created", eventMsg.c_str());
        addBroadcastEvent(bId, "queued", "Queued for delivery");

        // Mark the SOS request as resolved
        updateResidentAdminMsg(reqId, "resolved",
            (String("escalated_to_") + escalateTo).c_str(), uid);

        // ── Send SOS via XBee S2C (JSON command for node protocol) ────
        JsonDocument sosCmd;
        sosCmd["cmd"] = "BROADCAST_MSG";
        JsonObject sosParams = sosCmd["params"].to<JsonObject>();
        sosParams["from"] = "admin";
        sosParams["to"] = "all";
        sosParams["message"] = body;
        sosParams["subject"] = subject;
        sosParams["msg_type"] = escalateTo;
        String sosJson;
        serializeJson(sosCmd, sosJson);
        xbeeSendBroadcast(sosJson.c_str(), sosJson.length());

        // Log activity
        String logSubject = "[SOS Escalated] " + subject;
        logActivity(uid, logSubject.c_str(), body.c_str());

        JsonDocument resp;
        resp["message"] = "Escalated successfully";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  SOS: POST /api/sos-requests/{id}/dismiss                    │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/sos-requests\\/(\\d+)\\/dismiss$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        int reqId = request->pathArg(0).toInt();
        if (updateResidentAdminMsg(reqId, "dismissed", "handled_privately", uid)) {
            logActivity(uid, "[SOS Dismissed]", ("SOS request #" + String(reqId) + " dismissed").c_str());
            JsonDocument resp;
            resp["message"] = "Dismissed";
            String out; serializeJson(resp, out);
            request->send(200, "application/json", out);
        } else {
            sendJsonError(request, 404, "SOS request not found");
        }
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: GET /api/xbee/status                                  │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/status", HTTP_GET,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument resp;
        resp["tx_pin"]  = XBEE_TX_PIN;
        resp["rx_pin"]  = XBEE_RX_PIN;
        resp["baud"]    = XBEE_BAUD;
        resp["mode"]    = "API 1";
        resp["address"] = "broadcast (0x000000000000FFFF)";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: POST /api/xbee/test                                   │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/test", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String message = "HOPFOG_TEST|Hello from HopFog!";
        if (request->hasParam("message", true)) {
            String custom = request->getParam("message", true)->value();
            if (custom.length() > 0 && custom.length() <= 200) {
                message = "HOPFOG_TEST|" + custom;
            }
        }

        uint8_t frameId = xbeeSendBroadcast(message.c_str(), message.length());

        JsonDocument resp;
        if (frameId > 0) {
            resp["success"]  = true;
            resp["frame_id"] = frameId;
            resp["payload"]  = message;
            resp["length"]   = message.length();
            resp["message"]  = "Test message sent via XBee broadcast";
        } else {
            resp["success"] = false;
            resp["message"] = "Failed to send — payload too large or empty";
        }
        String out; serializeJson(resp, out);
        request->send(frameId > 0 ? 200 : 400, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: GET /api/xbee/rx-log — event log for serial monitor   │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/rx-log", HTTP_GET,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument resp;
        resp["rx_bytes_total"] = xbeeGetRxByteCount();
        resp["uptime_ms"]      = millis();
        // Include diagnostic summary for the serial monitor
        JsonObject diag = resp["diag"].to<JsonObject>();
        xbeeGetDiagnostics(diag);
        JsonArray log = resp["log"].to<JsonArray>();
        xbeeGetLog(log);
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: POST /api/xbee/send-raw — send arbitrary text        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/send-raw", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String text = "";
        if (request->hasParam("text", true)) {
            text = request->getParam("text", true)->value();
        }
        if (text.length() == 0 || text.length() > 400) {
            sendJsonError(request, 400, "Text required (1-400 chars)");
            return;
        }

        uint8_t frameId = xbeeSendBroadcast(text.c_str(), text.length());

        JsonDocument resp;
        resp["success"]  = (frameId > 0);
        resp["frame_id"] = frameId;
        resp["length"]   = text.length();
        String out; serializeJson(resp, out);
        request->send(frameId > 0 ? 200 : 400, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: GET /api/xbee/diagnostics — comprehensive HW diag     │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/diagnostics", HTTP_GET,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument resp;
        JsonObject diag = resp.to<JsonObject>();
        xbeeGetDiagnostics(diag);
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  XBEE: POST /api/xbee/loopback-test — UART self-test         │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/xbee/loopback-test", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String result;
        bool pass = xbeeRunLoopbackTest(result);

        JsonDocument resp;
        resp["success"] = pass;
        resp["result"]  = result;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ════════════════════════════════════════════════════════════════
    //  MOBILE APP ENDPOINTS (match HopFogMobile expected API)
    // ════════════════════════════════════════════════════════════════

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: GET /status — health check (no auth)                │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument resp;
        resp["online"] = true;
        resp["free_heap"] = ESP.getFreeHeap();
        resp["min_free_heap"] = ESP.getMinFreeHeap();
        resp["wifi_stations"] = WiFi.softAPgetStationNum();
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  TIME: POST /api/set-time — sync ESP32 clock from browser    │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/set-time", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        bool isJson = getJsonBody(request, jsonBody);
        unsigned long epoch = 0;
        if (isJson && jsonBody["epoch"].is<unsigned long>()) {
            epoch = jsonBody["epoch"].as<unsigned long>();
        } else if (request->hasParam("epoch", true)) {
            epoch = request->getParam("epoch", true)->value().toInt();
        }
        if (epoch < 1700000000UL) { // sanity: must be after Nov 14, 2023
            sendJsonError(request, 400, "Invalid epoch timestamp");
            return;
        }
        struct timeval tv;
        tv.tv_sec  = (time_t)epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        Serial.printf("[Time] Clock set to %lu (from browser)\n", epoch);
        JsonDocument resp;
        resp["success"] = true;
        resp["epoch"]   = epoch;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: GET /announcements — sent broadcasts as list        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/announcements", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument bDoc;
        readJsonArray(SD_BCASTS_FILE, bDoc);
        JsonDocument resp;
        JsonArray out = resp.to<JsonArray>();
        for (JsonObject b : bDoc.as<JsonArray>()) {
            String st = b["status"] | "";
            if (st != "sent" && st != "queued") continue; // only sent/queued
            JsonObject a = out.add<JsonObject>();
            a["id"]         = b["id"];
            a["title"]      = b["subject"] | "";
            a["message"]    = b["body"] | "";
            a["created_at"] = String(b["created_at"] | 0); // String: mobile app expects String? (Announcement.createdAt)
        }
        String outStr; serializeJson(resp, outStr);
        request->send(200, "application/json", outStr);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: GET /conversations?user_id=X                        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/conversations", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("user_id")) {
            sendJsonError(request, 400, "user_id required");
            return;
        }
        int userId = request->getParam("user_id")->value().toInt();

        JsonDocument convoDoc;
        readJsonArray(SD_CONVOS_FILE, convoDoc);

        JsonDocument dmDoc;
        readJsonArray(SD_DMS_FILE, dmDoc);

        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject c : convoDoc.as<JsonArray>()) {
            int u1 = c["user1_id"] | 0;
            int u2 = c["user2_id"] | 0;
            if (u1 != userId && u2 != userId) continue;

            int otherId = (u1 == userId) ? u2 : u1;
            String contactName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == otherId) {
                    contactName = u["username"] | "Unknown";
                    break;
                }
            }

            // Find last message in this conversation
            int convoId = c["id"] | 0;
            String lastMsg = "";
            String lastTs  = "";
            unsigned long latestTime = 0;
            for (JsonObject m : dmDoc.as<JsonArray>()) {
                if ((m["conversation_id"] | 0) != convoId) continue;
                unsigned long ts = m["sent_at"] | 0UL;
                if (ts >= latestTime) {
                    latestTime = ts;
                    lastMsg = m["message_text"] | "";
                    lastTs  = String(ts);
                }
            }

            JsonObject o = arr.add<JsonObject>();
            o["conversation_id"] = convoId;
            o["contact_name"]    = contactName;
            o["last_message"]    = lastMsg.length() > 0 ? lastMsg : JsonVariant();
            o["timestamp"]       = lastTs.length() > 0  ? lastTs  : JsonVariant();
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: GET /messages?conversation_id=X&user_id=Y           │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/messages", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("conversation_id") || !request->hasParam("user_id")) {
            sendJsonError(request, 400, "conversation_id and user_id required");
            return;
        }
        int convoId = request->getParam("conversation_id")->value().toInt();
        int userId  = request->getParam("user_id")->value().toInt();

        JsonDocument dmDoc;
        readJsonArray(SD_DMS_FILE, dmDoc);

        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            if ((m["conversation_id"] | 0) != convoId) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"]          = m["id"];
            o["message_text"]        = m["message_text"];
            o["sent_at"]             = String(m["sent_at"] | 0UL);
            o["sender_id"]           = senderId;
            o["is_from_current_user"] = (senderId == userId);
            o["sender_username"]     = senderName;
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: GET /new-messages?last_id=X&user_id=Y               │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/new-messages", HTTP_GET, [](AsyncWebServerRequest *request) {
        int lastId = request->hasParam("last_id") ? request->getParam("last_id")->value().toInt() : 0;
        int userId = request->hasParam("user_id") ? request->getParam("user_id")->value().toInt() : 0;

        // Find all conversations this user is part of
        JsonDocument convoDoc;
        readJsonArray(SD_CONVOS_FILE, convoDoc);

        JsonDocument dmDoc;
        readJsonArray(SD_DMS_FILE, dmDoc);

        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray arr = resp.to<JsonArray>();

        for (JsonObject m : dmDoc.as<JsonArray>()) {
            if ((m["id"] | 0) <= lastId) continue;

            // Check if user is part of this conversation
            int cid = m["conversation_id"] | 0;
            bool userInConvo = false;
            for (JsonObject c : convoDoc.as<JsonArray>()) {
                if ((c["id"] | 0) != cid) continue;
                int u1 = c["user1_id"] | 0;
                int u2 = c["user2_id"] | 0;
                if (u1 == userId || u2 == userId) userInConvo = true;
                break;
            }
            if (!userInConvo) continue;

            int senderId = m["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == senderId) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }

            JsonObject o = arr.add<JsonObject>();
            o["message_id"]          = m["id"];
            o["message_text"]        = m["message_text"];
            o["sent_at"]             = String(m["sent_at"] | 0UL);
            o["sender_id"]           = senderId;
            o["is_from_current_user"] = (senderId == userId);
            o["sender_username"]     = senderName;
        }

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: POST /send — send a direct message (JSON body)      │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/send", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        getJsonBody(request, jsonBody);

        int convoId  = getParamInt(request, jsonBody, "conversation_id");
        int senderId = getParamInt(request, jsonBody, "sender_id");
        String text  = getParam(request, jsonBody, "message_text");

        if (convoId <= 0 || senderId <= 0 || text.isEmpty()) {
            JsonDocument r; r["success"] = false; r["message"] = "Missing fields";
            String out; serializeJson(r, out);
            request->send(400, "application/json", out);
            return;
        }

        createDirectMessage(convoId, senderId, text.c_str());

        // ── Relay message via XBee S2C (JSON command for node protocol) ─
        // Check if this conversation is SOS-flagged to set the type.
        JsonDocument convosDoc;
        readJsonArray(SD_CONVOS_FILE, convosDoc);
        bool isSos = false;
        for (JsonObject c : convosDoc.as<JsonArray>()) {
            if ((c["id"] | 0) == convoId) {
                isSos = (c["is_sos"] | 0) == 1;
                break;
            }
        }
        JsonDocument senderDoc = getUserById(senderId);
        String senderName = senderDoc["username"] | "Unknown";

        JsonDocument dmCmd;
        dmCmd["cmd"] = "RELAY_CHAT_MSG";
        dmCmd["node_id"] = "admin";
        JsonObject dmParams = dmCmd["params"].to<JsonObject>();
        dmParams["conversation_id"] = convoId;
        dmParams["sender_id"] = senderId;
        dmParams["sender_name"] = senderName;
        dmParams["message_text"] = text;
        dmParams["is_sos"] = isSos;
        String dmJson;
        serializeJson(dmCmd, dmJson);
        xbeeSendBroadcast(dmJson.c_str(), dmJson.length());

        // Log activity
        String logSubject = isSos ? "[SOS Message] from " + senderName : "[DM] from " + senderName;
        logActivity(senderId, logSubject.c_str(), text.c_str());

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "Message sent";
        resp["secondsRemaining"] = 0;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: POST /create-chat — find or create conversation     │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/create-chat", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        getJsonBody(request, jsonBody);

        int user1Id = getParamInt(request, jsonBody, "user1_id");
        int user2Id = getParamInt(request, jsonBody, "user2_id");

        if (user1Id <= 0 || user2Id <= 0) {
            sendJsonError(request, 400, "user1_id and user2_id required");
            return;
        }

        int convoId = findOrCreateConversation(user1Id, user2Id, false);

        // Get contact name
        int otherId = user2Id;
        JsonDocument userDoc = getUserById(otherId);
        String contactName = userDoc["username"] | "Unknown";

        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"]    = contactName;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: POST /sos — find or create SOS chat with admin      │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/sos", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        getJsonBody(request, jsonBody);

        int userId = getParamInt(request, jsonBody, "user_id");
        if (userId <= 0) {
            sendJsonError(request, 400, "user_id required");
            return;
        }

        // Find the first admin user
        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);
        int adminId = 0;
        String adminName = "Admin";
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            String role = u["role"] | "";
            if (role == "admin" && (u["is_active"] | 0)) {
                adminId = u["id"] | 0;
                adminName = u["username"] | "Admin";
                break;
            }
        }

        if (adminId == 0) {
            sendJsonError(request, 500, "No admin user found");
            return;
        }

        int convoId = findOrCreateConversation(userId, adminId, true);

        // ── Send SOS alert via XBee S2C ─────────────────────────────
        // Look up the triggering user's name for the alert payload.
        JsonDocument userDoc = getUserById(userId);
        String userName = userDoc["username"] | "Unknown";
        // Send SOS alert via XBee in JSON format (for node protocol)
        JsonDocument sosAlertCmd;
        sosAlertCmd["cmd"] = "SOS_ALERT";
        sosAlertCmd["node_id"] = "admin";
        JsonObject sosAlertParams = sosAlertCmd["params"].to<JsonObject>();
        sosAlertParams["user_id"] = userId;
        sosAlertParams["conversation_id"] = convoId;
        sosAlertParams["username"] = userName;
        String sosAlertJson;
        serializeJson(sosAlertCmd, sosAlertJson);
        xbeeSendBroadcast(sosAlertJson.c_str(), sosAlertJson.length());

        // Log activity
        String logSubject = "[SOS Alert] " + userName + " triggered SOS";
        logActivity(userId, logSubject.c_str(), "SOS activated via mobile app");

        JsonDocument resp;
        resp["conversation_id"] = convoId;
        resp["contact_name"]    = adminName;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: POST /agree-sos — record SOS agreement              │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/agree-sos", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        getJsonBody(request, jsonBody);

        int userId = getParamInt(request, jsonBody, "user_id");
        if (userId <= 0) {
            JsonDocument r; r["success"] = false; r["message"] = "user_id required";
            String out; serializeJson(r, out);
            request->send(400, "application/json", out);
            return;
        }

        updateUserIntField(userId, "has_agreed_sos", 1);

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "SOS agreement recorded";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  MOBILE: POST /change-password (root path, JSON body)        │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/change-password", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument jsonBody;
        getJsonBody(request, jsonBody);

        int userId  = getParamInt(request, jsonBody, "user_id");
        String oldPw = getParam(request, jsonBody, "old_password");
        String newPw = getParam(request, jsonBody, "new_password");

        if (userId <= 0 || oldPw.isEmpty() || newPw.isEmpty()) {
            JsonDocument r; r["success"] = false; r["message"] = "Missing fields";
            String out; serializeJson(r, out);
            request->send(400, "application/json", out);
            return;
        }
        if (newPw.length() < 6) {
            JsonDocument r; r["success"] = false; r["message"] = "Password must be >= 6 characters";
            String out; serializeJson(r, out);
            request->send(400, "application/json", out);
            return;
        }

        JsonDocument userDoc = getUserById(userId);
        if (userDoc.isNull() || userDoc.size() == 0) {
            JsonDocument r; r["success"] = false; r["message"] = "User not found";
            String out; serializeJson(r, out);
            request->send(404, "application/json", out);
            return;
        }

        String storedHash = userDoc["password_hash"] | "";
        if (!verifyPassword(oldPw, storedHash)) {
            JsonDocument r; r["success"] = false; r["message"] = "Current password is incorrect";
            String out; serializeJson(r, out);
            request->send(400, "application/json", out);
            return;
        }

        String newHash = hashPassword(newPw);
        updateUserField(userId, "password_hash", newHash.c_str());

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "Password changed successfully";
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    }, NULL, jsonBodyHandler);

    // ╭───────────────────────────────────────────────────────────────╮
    // │  NODE MANAGEMENT: GET /api/nodes                              │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument resp;
        resp["total"]  = nodeProtocolTotalCount();
        resp["active"] = nodeProtocolActiveCount();
        JsonArray arr = resp["nodes"].to<JsonArray>();
        nodeProtocolGetNodes(arr);
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  NODE MANAGEMENT: POST /api/nodes/{id}/sync                   │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/nodes\\/([^/]+)\\/sync$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String nodeId = request->pathArg(0);
        nodeProtocolTriggerSync(nodeId.c_str());

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "Sync request sent to " + nodeId;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    // ╭───────────────────────────────────────────────────────────────╮
    // │  NODE MANAGEMENT: POST /api/nodes/{id}/get-stats              │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("^\\/api\\/nodes\\/([^/]+)\\/get-stats$", HTTP_POST,
              [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        String nodeId = request->pathArg(0);
        JsonDocument cmd;
        cmd["cmd"] = "GET_STATS";
        cmd["node_id"] = nodeId;
        String json;
        serializeJson(cmd, json);
        xbeeSendBroadcast(json.c_str(), json.length());

        JsonDocument resp;
        resp["success"] = true;
        resp["message"] = "Stats request sent to " + nodeId;
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
    });

    Serial.println("[HTTP] API routes registered");
}
