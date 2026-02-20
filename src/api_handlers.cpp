/*
 * api_handlers.cpp — REST API endpoints (mirrors original FastAPI routes)
 */

#include "api_handlers.h"
#include "sd_storage.h"
#include "auth.h"
#include "config.h"

#include <ArduinoJson.h>
#ifdef USE_SD_MMC
  #include <SD_MMC.h>
#else
  #include <SD.h>
#endif
#include <time.h>

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

// ── Register routes ────────────────────────────────────────────────

void registerApiRoutes(AsyncWebServer &server) {

    // ╭───────────────────────────────────────────────────────────────╮
    // │  AUTH: POST /login                                           │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
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
    });

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

        JsonDocument msgDoc;
        readJsonArray(SD_MSGS_FILE, msgDoc);
        int totalMessages = msgDoc.as<JsonArray>().size();

        JsonDocument userDoc = getUserById(uid);

        JsonDocument resp;
        resp["fog_nodes_count"]  = fogCount;
        resp["active_fog_nodes"] = activeCount;
        resp["inactive_fog_nodes"] = fogCount - activeCount;
        resp["people_connected"]   = totalConnected;
        resp["total_messages"]     = totalMessages;
        resp["storage_display"]    = (storageTotal > 0)
            ? String(storageUsed, 1) + "GB / " + String(storageTotal, 1) + "GB"
            : "N/A";
        resp["current_user"]       = userDoc["username"] | "admin";

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

        // Enrich with sender username
        JsonDocument usersDoc;
        readJsonArray(SD_USERS_FILE, usersDoc);

        JsonDocument resp;
        JsonArray out = resp.to<JsonArray>();

        for (JsonObject msg : doc.as<JsonArray>()) {
            JsonObject m = out.add<JsonObject>();
            m["id"]         = msg["id"];
            m["body"]       = msg["body"];
            m["subject"]    = msg["subject"];
            m["created_at"] = msg["created_at"];

            int sid = msg["sender_id"] | 0;
            String senderName = "Unknown";
            for (JsonObject u : usersDoc.as<JsonArray>()) {
                if ((u["id"] | 0) == sid) {
                    senderName = u["username"] | "Unknown";
                    break;
                }
            }
            m["from"] = senderName;

            // Resolve recipient usernames
            JsonArray toArr = m["to"].to<JsonArray>();
            if (msg["recipients"].is<JsonArray>()) {
                for (JsonObject r : msg["recipients"].as<JsonArray>()) {
                    int rid = r["user_id"] | 0;
                    for (JsonObject u : usersDoc.as<JsonArray>()) {
                        if ((u["id"] | 0) == rid) {
                            toArr.add(u["username"] | "Unknown");
                            break;
                        }
                    }
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
    // │  BROADCASTS: GET /api/broadcasts                             │
    // ╰───────────────────────────────────────────────────────────────╯
    server.on("/api/broadcasts", HTTP_GET, [](AsyncWebServerRequest *request) {
        int uid = authenticateRequest(request);
        if (uid < 0) { sendJsonError(request, 401, "Unauthorized"); return; }

        JsonDocument doc;
        readJsonArray(SD_BCASTS_FILE, doc);

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
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

        int priority = 10;
        if (msgType == "sos")   priority = 100;
        else if (msgType == "alert") priority = 50;

        int id = createBroadcast(uid, msgType.c_str(), severity.c_str(),
                                 audience.c_str(), subject.c_str(), body.c_str(),
                                 status.c_str(), priority);

        JsonDocument resp;
        resp["message"]      = "Broadcast created";
        resp["broadcast_id"] = id;
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

    Serial.println("[HTTP] API routes registered");
}
