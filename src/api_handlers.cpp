/*
 * api_handlers.cpp — REST API endpoints (mirrors original FastAPI routes)
 */

#include "api_handlers.h"
#include "sd_storage.h"
#include "auth.h"
#include "config.h"
#include "xbee_comm.h"

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

        JsonDocument bcDoc;
        readJsonArray(SD_BCASTS_FILE, bcDoc);
        int totalSosAlerts = 0;
        for (JsonObject b : bcDoc.as<JsonArray>()) {
            const char* sev = b["severity"] | "";
            if (strcmp(sev, "warning") == 0 || strcmp(sev, "critical") == 0)
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

        // ── Send via XBee S2C (ZigBee broadcast) ────────────────────
        // Payload format: "TYPE|SUBJECT|BODY" — pipe-delimited so the
        // receiving XBee node can parse msg_type, subject, and body.
        String xbeePayload = msgType + "|" + subject + "|" + body;
        xbeeSendBroadcast(xbeePayload.c_str(), xbeePayload.length());

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

        // Enrich each broadcast with recipient status counts
        JsonDocument resp;
        JsonArray out = resp.to<JsonArray>();
        for (JsonObject b : doc.as<JsonArray>()) {
            JsonObject o = out.add<JsonObject>();
            for (JsonPair kv : b) {
                o[kv.key()] = kv.value();
            }
            int total=0, queued=0, sent=0, delivered=0, readCount=0, failed=0;
            getRecipientStatusCounts(b["id"] | 0, total, queued, sent, delivered, readCount, failed);
            JsonObject sc = o["status_counts"].to<JsonObject>();
            sc["total"]     = total;
            sc["queued"]    = queued;
            sc["sent"]      = sent;
            sc["delivered"] = delivered;
            sc["read"]      = readCount;
            sc["failed"]    = failed;
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
            String xbeePayload = msgType + "|" + subject + "|" + body;
            uint8_t frameId = xbeeSendBroadcast(xbeePayload.c_str(), xbeePayload.length());
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

        // ── Send SOS via XBee S2C (ZigBee broadcast) ────────────────
        // Same pipe-delimited format as mark_sent: "TYPE|SUBJECT|BODY"
        String xbeePayload = escalateTo + "|" + subject + "|" + body;
        xbeeSendBroadcast(xbeePayload.c_str(), xbeePayload.length());

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

    Serial.println("[HTTP] API routes registered");
}
