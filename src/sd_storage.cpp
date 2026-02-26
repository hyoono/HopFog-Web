/*
 * sd_storage.cpp — JSON-based data storage on SD card
 */

#include "sd_storage.h"
#include "config.h"

#ifdef USE_SD_MMC
  #include <SD_MMC.h>
  #define SD_FS SD_MMC
#else
  #include <SD.h>
  #include <SPI.h>
  #define SD_FS SD
#endif

#include <ArduinoJson.h>
#include <time.h>

// Return current epoch timestamp (seconds since 1970-01-01)
static unsigned long currentTimestamp() {
    time_t now;
    time(&now);
    return (unsigned long)now;
}

// ── Helpers ─────────────────────────────────────────────────────────

static void ensureDir(const char *dir) {
    if (!SD_FS.exists(dir)) {
        SD_FS.mkdir(dir);
        Serial.printf("[SD] Created directory: %s\n", dir);
    }
}

static void seedFileIfMissing(const char *path) {
    if (!SD_FS.exists(path)) {
        File f = SD_FS.open(path, FILE_WRITE);
        if (f) {
            f.print("[]");
            f.close();
            Serial.printf("[SD] Seeded empty JSON array: %s\n", path);
        }
    }
}

// ── Init ────────────────────────────────────────────────────────────

bool initSDCard() {
    Serial.println("[SD] Initialising SD card …");

#ifdef USE_SD_MMC
    // ESP32-CAM: use 1-bit SD_MMC mode (GPIO 2=DATA0, 14=CLK, 15=CMD)
    // Disable the on-board flash LED (GPIO 4) to avoid SD bus conflicts
    pinMode(ESP32CAM_FLASH_PIN, OUTPUT);
    digitalWrite(ESP32CAM_FLASH_PIN, LOW);

    if (!SD_MMC.begin("/sdcard", true)) {   // true = 1-bit mode
        Serial.println("[SD] SD_MMC mount failed!");
        return false;
    }
    Serial.println("[SD] SD_MMC 1-bit mode — mounted OK");
#else
    // Generic ESP32: SPI mode with configurable CS pin
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] Mount failed!");
        return false;
    }
#endif

    uint64_t totalBytes = SD_FS.totalBytes();
    uint64_t usedBytes  = SD_FS.usedBytes();
    Serial.printf("[SD] Mounted — Total: %llu MB, Used: %llu MB\n",
                  totalBytes / (1024 * 1024), usedBytes / (1024 * 1024));

    // Ensure directory structure
    ensureDir(SD_WWW_DIR);
    ensureDir(SD_DB_DIR);

    // Seed empty data files
    seedFileIfMissing(SD_USERS_FILE);
    seedFileIfMissing(SD_MSGS_FILE);
    seedFileIfMissing(SD_FOG_FILE);
    seedFileIfMissing(SD_BCASTS_FILE);
    seedFileIfMissing(SD_RECIPS_FILE);
    seedFileIfMissing(SD_EVENTS_FILE);
    seedFileIfMissing(SD_RES_MSG_FILE);
    seedFileIfMissing(SD_CONVOS_FILE);
    seedFileIfMissing(SD_DMS_FILE);

    return true;
}

// ── Generic JSON helpers ────────────────────────────────────────────

bool readJsonArray(const char *path, JsonDocument &doc) {
    File f = SD_FS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] Cannot open %s for reading\n", path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[SD] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

bool writeJsonArray(const char *path, const JsonDocument &doc) {
    File f = SD_FS.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[SD] Cannot open %s for writing\n", path);
        return false;
    }
    serializeJson(doc, f);
    f.close();
    return true;
}

int nextId(const char *path) {
    JsonDocument doc;
    if (!readJsonArray(path, doc)) return 1;
    JsonArray arr = doc.as<JsonArray>();
    int maxId = 0;
    for (JsonObject obj : arr) {
        int id = obj["id"] | 0;
        if (id > maxId) maxId = id;
    }
    return maxId + 1;
}

// ── User helpers ────────────────────────────────────────────────────

bool userEmailExists(const char *email) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return false;
    for (JsonObject u : doc.as<JsonArray>()) {
        if (strcmp(u["email"] | "", email) == 0) return true;
    }
    return false;
}

bool userNameExists(const char *username) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return false;
    for (JsonObject u : doc.as<JsonArray>()) {
        if (strcmp(u["username"] | "", username) == 0) return true;
    }
    return false;
}

JsonDocument getUserByEmail(const char *email) {
    JsonDocument result;
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return result;
    for (JsonObject u : doc.as<JsonArray>()) {
        if (strcmp(u["email"] | "", email) == 0) {
            result.set(u);
            return result;
        }
    }
    return result;
}

JsonDocument getUserById(int id) {
    JsonDocument result;
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return result;
    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == id) {
            result.set(u);
            return result;
        }
    }
    return result;
}

int createUser(const char *username, const char *email,
               const char *passwordHash, const char *role, bool active) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_USERS_FILE);

    JsonObject user = arr.add<JsonObject>();
    user["id"]            = id;
    user["username"]      = username;
    user["email"]         = email;
    user["password_hash"] = passwordHash;
    user["role"]          = role;
    user["is_active"]      = active ? 1 : 0;
    user["has_agreed_sos"] = 0;
    user["created_at"]     = currentTimestamp();

    writeJsonArray(SD_USERS_FILE, doc);
    Serial.printf("[SD] Created user id=%d username=%s\n", id, username);
    return id;
}

bool updateUserField(int userId, const char *field, const char *value) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return false;
    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == userId) {
            u[field] = value;
            return writeJsonArray(SD_USERS_FILE, doc);
        }
    }
    return false;
}

bool toggleUserActive(int userId) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return false;
    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == userId) {
            int current = u["is_active"] | 0;
            u["is_active"] = current ? 0 : 1;
            return writeJsonArray(SD_USERS_FILE, doc);
        }
    }
    return false;
}

// ── Message helpers ─────────────────────────────────────────────────

int createMessage(int senderId, const char *subject, const char *body,
                  JsonArray recipientIds) {
    JsonDocument doc;
    if (!readJsonArray(SD_MSGS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_MSGS_FILE);

    JsonObject msg = arr.add<JsonObject>();
    msg["id"]         = id;
    msg["sender_id"]  = senderId;
    msg["subject"]    = subject;
    msg["body"]       = body;
    msg["created_at"] = currentTimestamp();

    JsonArray recips = msg["recipients"].to<JsonArray>();
    for (JsonVariant v : recipientIds) {
        JsonObject r = recips.add<JsonObject>();
        r["user_id"] = v.as<int>();
        r["status"]  = "sent";
    }

    writeJsonArray(SD_MSGS_FILE, doc);
    return id;
}

int logActivity(int actorId, const char *subject, const char *body) {
    JsonDocument tmp;
    JsonArray empty = tmp.to<JsonArray>();
    return createMessage(actorId, subject, body, empty);
}

bool deleteMessage(int messageId) {
    JsonDocument doc;
    if (!readJsonArray(SD_MSGS_FILE, doc)) return false;
    JsonArray arr = doc.as<JsonArray>();

    for (size_t i = 0; i < arr.size(); i++) {
        if ((arr[i]["id"] | 0) == messageId) {
            arr.remove(i);
            return writeJsonArray(SD_MSGS_FILE, doc);
        }
    }
    return false;
}

// ── Fog device helpers ──────────────────────────────────────────────

int registerFogDevice(const char *name) {
    JsonDocument doc;
    if (!readJsonArray(SD_FOG_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();

    // Check if device already exists by name
    for (JsonObject d : arr) {
        if (strcmp(d["device_name"] | "", name) == 0) {
            d["status"] = "active";
            writeJsonArray(SD_FOG_FILE, doc);
            return d["id"] | 0;
        }
    }

    int id = nextId(SD_FOG_FILE);
    JsonObject dev = arr.add<JsonObject>();
    dev["id"]              = id;
    dev["device_name"]     = name;
    dev["status"]          = "active";
    dev["storage_total"]   = "N/A";
    dev["storage_used"]    = "N/A";
    dev["connected_users"] = 0;

    writeJsonArray(SD_FOG_FILE, doc);
    return id;
}

bool updateFogDeviceStatus(int deviceId, const char *status,
                           const char *storageUsed, const char *storageTotal,
                           int connectedUsers) {
    JsonDocument doc;
    if (!readJsonArray(SD_FOG_FILE, doc)) return false;
    for (JsonObject d : doc.as<JsonArray>()) {
        if ((d["id"] | 0) == deviceId) {
            d["status"]          = status;
            d["storage_used"]    = storageUsed;
            d["storage_total"]   = storageTotal;
            d["connected_users"] = connectedUsers;
            return writeJsonArray(SD_FOG_FILE, doc);
        }
    }
    return false;
}

bool disconnectFogDevice(int deviceId) {
    JsonDocument doc;
    if (!readJsonArray(SD_FOG_FILE, doc)) return false;
    for (JsonObject d : doc.as<JsonArray>()) {
        if ((d["id"] | 0) == deviceId) {
            d["status"]          = "inactive";
            d["connected_users"] = 0;
            return writeJsonArray(SD_FOG_FILE, doc);
        }
    }
    return false;
}

// ── Broadcast helpers ───────────────────────────────────────────────

int createBroadcast(int createdBy, const char *msgType, const char *severity,
                    const char *audience, const char *subject, const char *body,
                    const char *status, int priority, int ttlHours) {
    JsonDocument doc;
    if (!readJsonArray(SD_BCASTS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_BCASTS_FILE);

    unsigned long now = currentTimestamp();
    JsonObject bc = arr.add<JsonObject>();
    bc["id"]         = id;
    bc["created_by"] = createdBy;
    bc["msg_type"]   = msgType;
    bc["severity"]   = severity;
    bc["audience"]   = audience;
    bc["subject"]    = subject;
    bc["body"]       = body;
    bc["status"]     = status;
    bc["priority"]   = priority;
    bc["created_at"] = now;
    bc["updated_at"] = now;
    if (ttlHours > 0) {
        bc["ttl_hours"]      = ttlHours;
        bc["ttl_expires_at"] = now + (unsigned long)ttlHours * 3600UL;
    }

    writeJsonArray(SD_BCASTS_FILE, doc);
    return id;
}

bool updateBroadcastStatus(int broadcastId, const char *newStatus) {
    JsonDocument doc;
    if (!readJsonArray(SD_BCASTS_FILE, doc)) return false;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject b : arr) {
        if ((b["id"] | 0) == broadcastId) {
            b["status"]     = newStatus;
            b["updated_at"] = currentTimestamp();
            return writeJsonArray(SD_BCASTS_FILE, doc);
        }
    }
    return false;
}

bool updateResidentAdminMsg(int msgId, const char *status, const char *adminAction, int handledBy) {
    JsonDocument doc;
    if (!readJsonArray(SD_RES_MSG_FILE, doc)) return false;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject m : arr) {
        if ((m["id"] | 0) == msgId) {
            m["status"] = status;
            m["admin_action"] = adminAction;
            m["handled_by"] = handledBy;
            m["handled_at"] = currentTimestamp();
            return writeJsonArray(SD_RES_MSG_FILE, doc);
        }
    }
    return false;
}

// ── Broadcast recipient helpers ─────────────────────────────────────

void createRecipientsForBroadcast(int broadcastId) {
    // Read all users and create a recipient entry for each active resident
    JsonDocument usersDoc;
    if (!readJsonArray(SD_USERS_FILE, usersDoc)) return;

    JsonDocument recipDoc;
    if (!readJsonArray(SD_RECIPS_FILE, recipDoc)) {
        recipDoc.to<JsonArray>();
    }
    JsonArray arr = recipDoc.as<JsonArray>();
    int nextRId = nextId(SD_RECIPS_FILE);
    unsigned long now = currentTimestamp();

    for (JsonObject u : usersDoc.as<JsonArray>()) {
        int isActive = u["is_active"] | 0;
        if (!isActive) continue;
        String role = u["role"] | "";
        if (role == "admin") continue;  // only residents/mobile users

        JsonObject r = arr.add<JsonObject>();
        r["id"]           = nextRId++;
        r["broadcast_id"] = broadcastId;
        r["user_id"]      = u["id"] | 0;
        r["status"]       = "queued";
        r["attempts"]     = 0;
        r["created_at"]   = now;
    }

    writeJsonArray(SD_RECIPS_FILE, recipDoc);
}

void updateRecipientsStatus(int broadcastId, const char *newStatus) {
    JsonDocument doc;
    if (!readJsonArray(SD_RECIPS_FILE, doc)) return;
    JsonArray arr = doc.as<JsonArray>();
    unsigned long now = currentTimestamp();
    bool changed = false;

    for (JsonObject r : arr) {
        if ((r["broadcast_id"] | 0) == broadcastId) {
            r["status"]   = newStatus;
            r["attempts"] = (r["attempts"] | 0) + 1;
            r["last_attempt_at"] = now;
            if (strcmp(newStatus, "sent") == 0)      r["sent_at"]      = now;
            if (strcmp(newStatus, "delivered") == 0)  r["delivered_at"] = now;
            if (strcmp(newStatus, "read") == 0)       r["read_at"]      = now;
            if (strcmp(newStatus, "failed") == 0)     r["fail_reason"]  = "manual";
            changed = true;
        }
    }

    if (changed) writeJsonArray(SD_RECIPS_FILE, doc);
}

void getRecipientStatusCounts(int broadcastId, int &total, int &queued,
                              int &sent, int &delivered, int &readCount, int &failed) {
    total = queued = sent = delivered = readCount = failed = 0;
    JsonDocument doc;
    if (!readJsonArray(SD_RECIPS_FILE, doc)) return;

    for (JsonObject r : doc.as<JsonArray>()) {
        if ((r["broadcast_id"] | 0) != broadcastId) continue;
        total++;
        String s = r["status"] | "queued";
        if (s == "queued")         queued++;
        else if (s == "sent")      sent++;
        else if (s == "delivered") delivered++;
        else if (s == "read")      readCount++;
        else if (s == "failed")    failed++;
    }
}

void getAllRecipientStatusCounts(JsonDocument &outMap) {
    outMap.to<JsonObject>();
    JsonDocument doc;
    if (!readJsonArray(SD_RECIPS_FILE, doc)) return;

    for (JsonObject r : doc.as<JsonArray>()) {
        String key = String(r["broadcast_id"] | 0);
        if (!outMap[key].is<JsonObject>()) {
            JsonObject sc = outMap[key].to<JsonObject>();
            sc["total"]=0; sc["queued"]=0; sc["sent"]=0;
            sc["delivered"]=0; sc["read"]=0; sc["failed"]=0;
        }
        JsonObject sc = outMap[key].as<JsonObject>();
        sc["total"] = (sc["total"] | 0) + 1;
        String s = r["status"] | "queued";
        if      (s == "queued")    sc["queued"]    = (sc["queued"]    | 0) + 1;
        else if (s == "sent")      sc["sent"]      = (sc["sent"]      | 0) + 1;
        else if (s == "delivered") sc["delivered"]  = (sc["delivered"] | 0) + 1;
        else if (s == "read")      sc["read"]      = (sc["read"]      | 0) + 1;
        else if (s == "failed")    sc["failed"]    = (sc["failed"]    | 0) + 1;
    }
}

// ── Broadcast event helpers ─────────────────────────────────────────

void addBroadcastEvent(int broadcastId, const char *eventType, const char *message) {
    JsonDocument doc;
    if (!readJsonArray(SD_EVENTS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_EVENTS_FILE);

    JsonObject ev = arr.add<JsonObject>();
    ev["id"]           = id;
    ev["broadcast_id"] = broadcastId;
    ev["event_type"]   = eventType;
    if (message) ev["message"] = message;
    ev["created_at"]   = currentTimestamp();

    writeJsonArray(SD_EVENTS_FILE, doc);
}

void getBroadcastEvents(int broadcastId, JsonDocument &outDoc) {
    JsonDocument doc;
    if (!readJsonArray(SD_EVENTS_FILE, doc)) {
        outDoc.to<JsonArray>();
        return;
    }
    JsonArray out = outDoc.to<JsonArray>();
    for (JsonObject ev : doc.as<JsonArray>()) {
        if ((ev["broadcast_id"] | 0) == broadcastId) {
            out.add(ev);
        }
    }
}

// ── Conversation helpers (mobile chat) ──────────────────────────────

int findOrCreateConversation(int user1Id, int user2Id, bool isSos) {
    JsonDocument doc;
    if (!readJsonArray(SD_CONVOS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();

    // Find existing conversation between these two users
    for (JsonObject c : arr) {
        int u1 = c["user1_id"] | 0;
        int u2 = c["user2_id"] | 0;
        if ((u1 == user1Id && u2 == user2Id) ||
            (u1 == user2Id && u2 == user1Id)) {
            return c["id"] | 0;
        }
    }

    // Create new
    int id = nextId(SD_CONVOS_FILE);
    JsonObject c = arr.add<JsonObject>();
    c["id"]         = id;
    c["user1_id"]   = user1Id;
    c["user2_id"]   = user2Id;
    c["is_sos"]     = isSos ? 1 : 0;
    c["created_at"] = currentTimestamp();

    writeJsonArray(SD_CONVOS_FILE, doc);
    Serial.printf("[SD] Created conversation id=%d between users %d and %d\n", id, user1Id, user2Id);
    return id;
}

// ── Direct message helpers (mobile chat) ────────────────────────────

int createDirectMessage(int conversationId, int senderId, const char *text) {
    JsonDocument doc;
    if (!readJsonArray(SD_DMS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_DMS_FILE);

    JsonObject msg = arr.add<JsonObject>();
    msg["id"]              = id;
    msg["conversation_id"] = conversationId;
    msg["sender_id"]       = senderId;
    msg["message_text"]    = text;
    msg["sent_at"]         = currentTimestamp();

    writeJsonArray(SD_DMS_FILE, doc);
    return id;
}

// ── Additional user helpers ─────────────────────────────────────────

JsonDocument getUserByUsername(const char *username) {
    JsonDocument result;
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return result;
    for (JsonObject u : doc.as<JsonArray>()) {
        if (strcmp(u["username"] | "", username) == 0) {
            result.set(u);
            return result;
        }
    }
    return result;
}

bool updateUserIntField(int userId, const char *field, int value) {
    JsonDocument doc;
    if (!readJsonArray(SD_USERS_FILE, doc)) return false;
    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == userId) {
            u[field] = value;
            return writeJsonArray(SD_USERS_FILE, doc);
        }
    }
    return false;
}
