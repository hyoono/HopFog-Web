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
    seedFileIfMissing(SD_RES_MSG_FILE);

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
    user["is_active"]     = active ? 1 : 0;
    user["created_at"]    = currentTimestamp();

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
                    const char *status, int priority) {
    JsonDocument doc;
    if (!readJsonArray(SD_BCASTS_FILE, doc)) {
        doc.to<JsonArray>();
    }
    JsonArray arr = doc.as<JsonArray>();
    int id = nextId(SD_BCASTS_FILE);

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
    bc["created_at"] = currentTimestamp();

    writeJsonArray(SD_BCASTS_FILE, doc);
    return id;
}

bool updateBroadcastStatus(int broadcastId, const char *newStatus) {
    JsonDocument doc;
    if (!readJsonArray(SD_BCASTS_FILE, doc)) return false;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject b : arr) {
        if ((b["id"] | 0) == broadcastId) {
            b["status"] = newStatus;
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
            return writeJsonArray(SD_RES_MSG_FILE, doc);
        }
    }
    return false;
}
