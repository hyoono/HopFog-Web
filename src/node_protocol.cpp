/*
 * node_protocol.cpp — Handle HopFog-Node JSON commands over XBee.
 *
 * CRITICAL: ZigBee broadcast max payload is ~84 bytes. All RESPONSES
 * are sent via unicast (xbeeSendTo) which supports fragmentation up
 * to ~255 bytes. Only discovery PINGs use broadcast.
 *
 * SYNC_DATA uses a NON-BLOCKING state machine to avoid starving
 * the WiFi/DNS stack. Each loop iteration sends at most one record
 * or SC chunk, then yields back to loop() so dnsServer and WiFi
 * can process packets. This prevents client disconnections.
 */

#include "node_protocol.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"

// ── Node registry (in-memory) ──────────────────────────────────────
static NodeInfo nodes[MAX_NODES];
static int nodeCount = 0;

// ── Helper: find or create a node slot ─────────────────────────────
static NodeInfo* findNode(const char* nodeId) {
    for (int i = 0; i < nodeCount; i++) {
        if (strcmp(nodes[i].node_id, nodeId) == 0) return &nodes[i];
    }
    return nullptr;
}

static NodeInfo* registerNode(const char* nodeId, const XBeeAddr& addr) {
    NodeInfo* n = findNode(nodeId);
    if (n) {
        n->xbeeAddr = addr;  // Update address
        return n;
    }
    if (nodeCount >= MAX_NODES) return nullptr;
    n = &nodes[nodeCount++];
    memset(n, 0, sizeof(NodeInfo));
    strncpy(n->node_id, nodeId, sizeof(n->node_id) - 1);
    strncpy(n->status, "active", sizeof(n->status) - 1);
    n->registered_at = millis();
    n->last_heartbeat = millis();
    n->xbeeAddr = addr;
    return n;
}

// ── Helper: send JSON command to a specific node (unicast) ─────────
static void sendReply(const XBeeAddr& dest, JsonDocument& doc) {
    String json;
    serializeJson(doc, json);

    if (dest.valid) {
        // Unicast: supports fragmentation, up to ~255 bytes
        xbeeSendTo(dest, json.c_str(), json.length());
    } else {
        // Fallback to broadcast if no known address
        xbeeSendBroadcast(json.c_str(), json.length());
    }
}

// ── Helper: send JSON as broadcast (for PING discovery only) ───────
static void sendBroadcast(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Public: send to ALL registered nodes via unicast ───────────────
// Unicast supports ~240 bytes (fragmentation). Falls back to broadcast
// if no nodes are registered yet.
void nodeProtocolSendToAllNodes(const char* json, size_t len) {
    int sent = 0;
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].xbeeAddr.valid) {
            xbeeSendTo(nodes[i].xbeeAddr, json, len);
            sent++;
            // Pace between nodes to avoid XBee TX buffer overload
            if (i < nodeCount - 1) delay(NODE_UNICAST_PACING_MS);
        }
    }
    if (sent == 0) {
        // No registered nodes — fall back to broadcast (may be truncated)
        xbeeSendBroadcast(json, len);
    }
}

// ── Command handlers ───────────────────────────────────────────────

static void handleRegister(const char* nodeId, JsonObject params,
                           const XBeeAddr& src) {
    NodeInfo* n = registerNode(nodeId, src);
    if (!n) {
        logMsg('E', "Registry full, cannot register %s", nodeId);
        return;
    }
    strncpy(n->device_name,
            params["device_name"] | params["name"] | nodeId,
            sizeof(n->device_name) - 1);
    strncpy(n->ip_address,
            params["ip_address"] | params["ip"] | "",
            sizeof(n->ip_address) - 1);
    strncpy(n->status, "active", sizeof(n->status) - 1);
    n->last_heartbeat = millis();

    // Reply REGISTER_ACK via unicast (~44 bytes)
    JsonDocument ack;
    ack["cmd"] = "REGISTER_ACK";
    ack["node_id"] = nodeId;
    sendReply(src, ack);
    logMsg('S', "Registered %s (addr %02X%02X)",
           nodeId, src.addr64[6], src.addr64[7]);
}

static void handleHeartbeat(const char* nodeId, JsonObject params,
                            const XBeeAddr& src) {
    NodeInfo* n = findNode(nodeId);
    if (!n) n = registerNode(nodeId, src);
    if (!n) return;
    n->last_heartbeat = millis();
    n->xbeeAddr = src;
    strncpy(n->status, "active", sizeof(n->status) - 1);
    strncpy(n->ip_address,
            params["ip_address"] | params["ip"] | n->ip_address,
            sizeof(n->ip_address) - 1);
    n->free_heap = params["free_heap"] | params["heap"] | 0;
    n->uptime = params["uptime"] | 0;

    // Reply PONG via unicast (~33 bytes)
    JsonDocument pong;
    pong["cmd"] = "PONG";
    pong["node_id"] = nodeId;
    sendReply(src, pong);
}

// ═══════════════════════════════════════════════════════════════════
// NON-BLOCKING SYNC STATE MACHINE
//
// Instead of blocking loop() for 10+ seconds with delay() calls,
// the sync engine sends ONE record/chunk per loop iteration, then
// yields so dnsServer, WiFi, and XBee can process. This prevents
// client disconnections during sync.
//
// IMPORTANT: Broadcasts.json is transformed to announcement format
// during sync. The node and mobile app expect {id, title, message,
// created_at} — NOT the full broadcast fields.
//
// INCREMENTAL: Auto-sync only sends records with id > last synced id.
// Manual "Send SYNC_DATA" triggers a FULL sync (all records).
//
// COMPACT KEYS: Messages use short keys to save ~30 bytes:
//   "c"="SD" (SYNC_DATA), "c"="SC" (continuation), "c"="DONE"
//   "n"=node_id, "p"=part, "s"=seq, "d"=record data
//   "n2"=record count (end-of-part), "k"=field key, "v"=field value
//
// State machine:
//   IDLE -> LOAD_PART -> SEND_RECORD -> SEND_SC -> SEND_COUNT -> (next part) -> SEND_DONE -> IDLE
// ═══════════════════════════════════════════════════════════════════

enum SyncState {
    SYNC_IDLE,
    SYNC_LOAD_PART,     // Load next part from SD
    SYNC_SEND_RECORD,   // Send one SYNC_DATA record (or skeleton)
    SYNC_SEND_SC,       // Send one SC continuation chunk
    SYNC_SEND_COUNT,    // Send count message for current part
    SYNC_SEND_DONE      // Send SYNC_DONE
};

static SyncState    syncState = SYNC_IDLE;
static char         syncNodeId[32] = "";
static XBeeAddr     syncDest = {};
static int          syncPartIndex = 0;       // 0-4 (users, announcements, ...)
static unsigned long syncLastSendMs = 0;     // Pacing timer
static bool         syncIsFullSync = false;  // true = manual full, false = incremental

// Per-part state
static JsonDocument syncPartDoc;             // Loaded JSON array for current part
static int          syncRecordIndex = 0;     // Current record in the array
static int          syncRecordCount = 0;     // Total records in current part
static int          syncSentCount = 0;       // Records sent so far

// SC continuation state (for oversized records)
static String scKeys[16];
static String scValues[16];
static int    scFieldCount = 0;
static int    scFieldIdx = 0;                // Current field being sent
static int    scOffset = 0;                  // Offset within current field value

// Incremental sync: track the highest ID synced per part
static int lastSyncedId[5] = {0, 0, 0, 0, 0};  // per part

static const char* syncPartNames[] = {
    "users", "announcements", "conversations", "chat_messages", "fog_nodes"
};
static const char* syncPartFiles[] = {
    SD_USERS_FILE, SD_BCASTS_FILE, SD_CONVOS_FILE, SD_DMS_FILE, SD_FOG_FILE
};
static const int SYNC_NUM_PARTS = 5;

// ── Transform broadcasts.json to mobile-friendly announcement format ──
// Admin stores: {id, created_by, msg_type, severity, audience, subject, body,
//                status, priority, created_at, updated_at, ttl_hours, ...}
// Mobile app expects: {id, title, message, created_at}
// This reduces each record from 280+ bytes to ~80 bytes.
static void transformBroadcastsToAnnouncements(JsonDocument& doc) {
    if (!doc.is<JsonArray>()) return;
    JsonArray arr = doc.as<JsonArray>();

    JsonDocument transformed;
    JsonArray out = transformed.to<JsonArray>();
    for (JsonObject b : arr) {
        String st = b["status"] | "";
        if (st != "sent" && st != "queued") continue;  // Only sent/queued
        JsonObject a = out.add<JsonObject>();
        a["id"]         = b["id"];
        a["title"]      = b["subject"] | "";
        a["message"]    = b["body"] | "";
        a["created_at"] = String(b["created_at"] | 0);
    }
    doc.set(transformed);
}

// ── Filter to only new records (id > threshold) for incremental sync ──
static void filterNewRecords(JsonDocument& doc, int minId) {
    if (minId <= 0) return;  // Full sync — don't filter
    if (!doc.is<JsonArray>()) return;

    JsonDocument filtered;
    JsonArray out = filtered.to<JsonArray>();
    for (JsonVariant v : doc.as<JsonArray>()) {
        JsonObject rec = v.as<JsonObject>();
        int recId = rec["id"] | 0;
        if (recId > minId) {
            out.add(rec);
        }
    }
    doc.set(filtered);
}

static void syncStart(const char* nodeId, const XBeeAddr& dest, bool fullSync) {
    strncpy(syncNodeId, nodeId, sizeof(syncNodeId) - 1);
    syncNodeId[sizeof(syncNodeId) - 1] = '\0';
    syncDest = dest;
    syncPartIndex = 0;
    syncIsFullSync = fullSync;
    syncState = SYNC_LOAD_PART;
    syncLastSendMs = millis();
    logMsg('S', "SYNC starting to %s (%s)", nodeId,
           fullSync ? "FULL" : "incremental");
}

// Called from nodeProtocolLoop() -- sends at most one message per call.
static void syncTick() {
    if (syncState == SYNC_IDLE) return;

    // Pacing: wait at least SYNC_CHUNK_DELAY_MS between sends
    if (millis() - syncLastSendMs < SYNC_CHUNK_DELAY_MS) return;

    switch (syncState) {

    case SYNC_LOAD_PART: {
        if (syncPartIndex >= SYNC_NUM_PARTS) {
            syncState = SYNC_SEND_DONE;
            return;
        }
        syncPartDoc.clear();
        readJsonArray(syncPartFiles[syncPartIndex], syncPartDoc);

        // Transform broadcasts → mobile-friendly announcements
        if (syncPartIndex == 1) {  // "announcements" part
            transformBroadcastsToAnnouncements(syncPartDoc);
        }

        // Incremental sync: filter to only new records
        if (!syncIsFullSync) {
            filterNewRecords(syncPartDoc, lastSyncedId[syncPartIndex]);
        }

        JsonArray arr = syncPartDoc.is<JsonArray>() ? syncPartDoc.as<JsonArray>()
                                                      : syncPartDoc.to<JsonArray>();
        syncRecordCount = arr.size();
        syncRecordIndex = 0;
        syncSentCount = 0;
        if (syncRecordCount == 0) {
            syncState = SYNC_SEND_COUNT;
        } else {
            syncState = SYNC_SEND_RECORD;
        }
        return;
    }

    case SYNC_SEND_RECORD: {
        if (syncRecordIndex >= syncRecordCount) {
            syncState = SYNC_SEND_COUNT;
            return;
        }

        JsonArray arr = syncPartDoc.as<JsonArray>();
        JsonObject rec = arr[syncRecordIndex].as<JsonObject>();

        // Track the highest ID for incremental sync
        int recId = rec["id"] | 0;
        if (recId > lastSyncedId[syncPartIndex]) {
            lastSyncedId[syncPartIndex] = recId;
        }

        // Build SYNC_DATA with compact keys:
        //   "c"="SD" (cmd), "n"=node_id, "p"=part, "s"=seq, "d"=record
        // Saves ~30 bytes vs full key names
        JsonDocument msg;
        msg["c"] = "SD";
        msg["n"] = syncNodeId;
        msg["p"] = syncPartNames[syncPartIndex];
        msg["s"] = syncSentCount;
        JsonObject d = msg["d"].to<JsonObject>();
        for (JsonPair kv : rec) {
            d[kv.key()] = kv.value();
        }

        String json;
        serializeJson(msg, json);

        if ((int)json.length() <= XBEE_UNICAST_MAX) {
            // Fits! Send as-is.
            sendReply(syncDest, msg);
            syncSentCount++;
            syncRecordIndex++;
            syncLastSendMs = millis();
            return;
        }

        // Record too large: collect string fields for SC chunking
        scFieldCount = 0;
        for (JsonPair kv : rec) {
            if (scFieldCount >= 16) break;
            if (!kv.value().is<const char*>()) continue;
            scKeys[scFieldCount] = kv.key().c_str();
            scValues[scFieldCount] = kv.value().as<const char*>();
            scFieldCount++;
        }

        // Send skeleton base (all strings emptied)
        JsonDocument skelMsg;
        skelMsg["c"] = "SD";
        skelMsg["n"] = syncNodeId;
        skelMsg["p"] = syncPartNames[syncPartIndex];
        skelMsg["s"] = syncSentCount;
        JsonObject skelD = skelMsg["d"].to<JsonObject>();
        for (JsonPair kv : rec) {
            if (kv.value().is<const char*>()) {
                skelD[kv.key()] = "";
            } else {
                skelD[kv.key()] = kv.value();
            }
        }
        sendReply(syncDest, skelMsg);
        syncLastSendMs = millis();

        // Prepare SC state
        scFieldIdx = 0;
        scOffset = 0;
        while (scFieldIdx < scFieldCount && scValues[scFieldIdx].length() == 0) {
            scFieldIdx++;
        }
        if (scFieldIdx < scFieldCount) {
            syncState = SYNC_SEND_SC;
        } else {
            syncSentCount++;
            syncRecordIndex++;
            syncState = SYNC_SEND_RECORD;
        }
        return;
    }

    case SYNC_SEND_SC: {
        if (scFieldIdx >= scFieldCount) {
            syncSentCount++;
            syncRecordIndex++;
            syncState = SYNC_SEND_RECORD;
            return;
        }

        int fullLen = scValues[scFieldIdx].length();
        if (scOffset >= fullLen) {
            scFieldIdx++;
            scOffset = 0;
            while (scFieldIdx < scFieldCount && scValues[scFieldIdx].length() == 0) {
                scFieldIdx++;
            }
            if (scFieldIdx >= scFieldCount) {
                syncSentCount++;
                syncRecordIndex++;
                syncState = SYNC_SEND_RECORD;
            }
            return;
        }

        JsonDocument cont;
        cont["c"] = "SC";
        cont["p"] = syncPartNames[syncPartIndex];
        cont["s"] = syncSentCount;
        cont["k"] = scKeys[scFieldIdx];

        // Dynamic chunk sizing
        int trySize = 130;
        int actualEnd = 0;
        String cJson;
        bool fits = false;
        while (trySize >= 10) {
            actualEnd = (scOffset + trySize < fullLen)
                        ? scOffset + trySize : fullLen;
            cont["v"] = scValues[scFieldIdx].substring(scOffset, actualEnd);
            serializeJson(cont, cJson);
            if ((int)cJson.length() <= XBEE_UNICAST_MAX) {
                fits = true;
                break;
            }
            trySize -= 10;
        }

        if (!fits) {
            logMsg('E', "SC chunk too large %s[%d].%s",
                   syncPartNames[syncPartIndex], syncSentCount,
                   scKeys[scFieldIdx].c_str());
            scFieldIdx++;
            scOffset = 0;
            return;
        }

        sendReply(syncDest, cont);
        syncLastSendMs = millis();
        scOffset = actualEnd;
        return;
    }

    case SYNC_SEND_COUNT: {
        JsonDocument countMsg;
        countMsg["c"] = "SD";
        countMsg["n"] = syncNodeId;
        countMsg["p"] = syncPartNames[syncPartIndex];
        countMsg["n2"] = syncSentCount;
        sendReply(syncDest, countMsg);
        syncLastSendMs = millis();

        logMsg('S', "Synced %s: %d records",
               syncPartNames[syncPartIndex], syncSentCount);

        syncPartIndex++;
        syncState = SYNC_LOAD_PART;
        return;
    }

    case SYNC_SEND_DONE: {
        JsonDocument done;
        done["c"] = "DONE";
        done["n"] = syncNodeId;
        sendReply(syncDest, done);
        syncLastSendMs = millis();

        logMsg('S', "SYNC complete to %s", syncNodeId);
        syncState = SYNC_IDLE;
        return;
    }

    default:
        syncState = SYNC_IDLE;
        return;
    }
}

static void handleSosAlert(const char* nodeId, JsonObject params) {
    int userId = params["user_id"] | 0;
    if (userId <= 0) return;

    JsonDocument doc;
    readJsonArray(SD_RES_MSG_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonDocument usersDoc;
    readJsonArray(SD_USERS_FILE, usersDoc);
    String username = "User " + String(userId);
    if (usersDoc.is<JsonArray>()) {
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            if ((u["id"] | 0) == userId) {
                username = u["username"] | username.c_str();
                break;
            }
        }
    }

    JsonObject sos = arr.add<JsonObject>();
    sos["id"] = newId;
    sos["sender_id"] = userId;
    sos["kind"] = "sos_request";
    sos["subject"] = "SOS from " + username + " (via " + String(nodeId) + ")";
    sos["body"] = "SOS alert received via fog node " + String(nodeId);
    sos["priority"] = 100;
    sos["status"] = "queued";
    sos["created_at"] = (long)time(nullptr);

    writeJsonArray(SD_RES_MSG_FILE, doc);
    logMsg('S', "SOS from user %d via %s", userId, nodeId);
}

static void handleChangePassword(JsonObject params) {
    int userId = params["user_id"] | 0;
    const char* newPw = params["new_password"] | "";
    if (userId <= 0 || strlen(newPw) == 0) return;

    JsonDocument doc;
    readJsonArray(SD_USERS_FILE, doc);
    if (!doc.is<JsonArray>()) return;

    for (JsonObject u : doc.as<JsonArray>()) {
        if ((u["id"] | 0) == userId) {
            u["password_hash"] = newPw;
            writeJsonArray(SD_USERS_FILE, doc);
            return;
        }
    }
}

static void handleRelayFogNode(JsonObject params) {
    const char* name = params["device_name"] | "";
    if (strlen(name) == 0) return;

    JsonDocument doc;
    readJsonArray(SD_FOG_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    for (JsonObject d : arr) {
        if (strcmp(d["device_name"] | "", name) == 0) {
            d["status"] = params["status"] | "active";
            writeJsonArray(SD_FOG_FILE, doc);
            return;
        }
    }

    int newId = 1;
    for (JsonObject d : arr) {
        int id = d["id"] | 0;
        if (id >= newId) newId = id + 1;
    }
    JsonObject dev = arr.add<JsonObject>();
    dev["id"] = newId;
    dev["device_name"] = name;
    dev["status"] = params["status"] | "active";
    writeJsonArray(SD_FOG_FILE, doc);
}

static void handleRelayChatMsg(const char* nodeId, JsonObject params) {
    int convId = params["conversation_id"] | 0;
    int senderId = params["sender_id"] | 0;
    const char* text = params["message_text"] | "";
    if (convId <= 0 || senderId <= 0 || strlen(text) == 0) return;

    JsonDocument doc;
    readJsonArray(SD_DMS_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonObject msg = arr.add<JsonObject>();
    msg["id"] = newId;
    msg["conversation_id"] = convId;
    msg["sender_id"] = senderId;
    msg["message_text"] = text;
    msg["sent_at"] = (long)time(nullptr);

    writeJsonArray(SD_DMS_FILE, doc);
}

static void handleStatsResponse(const char* nodeId, JsonObject params) {
    NodeInfo* n = findNode(nodeId);
    if (!n) return;
    n->free_heap = params["free_heap"] | 0;
    n->uptime = params["uptime"] | 0;
}

// ── SYNC_BACK: Node -> Admin data merge ────────────────────────────
//
// Receives records from a node and merges them into admin's SD card.
// Matches by "id" field: new records are added, existing are skipped.
// Same format as SYNC_DATA: part="chat_messages", seq=N, d={record}
// Count message: part="chat_messages", n=5
// Final: SYNC_BACK_DONE

// Temporary accumulation buffers for incoming SYNC_BACK data
static JsonDocument sbUsers;
static JsonDocument sbAnnouncements;
static JsonDocument sbConversations;
static JsonDocument sbChatMessages;
static JsonDocument sbFogNodes;
static bool sbInProgress = false;

static void initSyncBackBuffers() {
    sbUsers.to<JsonArray>();
    sbAnnouncements.to<JsonArray>();
    sbConversations.to<JsonArray>();
    sbChatMessages.to<JsonArray>();
    sbFogNodes.to<JsonArray>();
    sbInProgress = true;
}

static JsonDocument* getSyncBackBuffer(const char* part) {
    if (strcmp(part, "users") == 0) return &sbUsers;
    if (strcmp(part, "announcements") == 0) return &sbAnnouncements;
    if (strcmp(part, "conversations") == 0) return &sbConversations;
    if (strcmp(part, "chat_messages") == 0) return &sbChatMessages;
    if (strcmp(part, "fog_nodes") == 0) return &sbFogNodes;
    return nullptr;
}

static const char* partToSdFile(const char* part) {
    if (strcmp(part, "users") == 0) return SD_USERS_FILE;
    if (strcmp(part, "announcements") == 0) return SD_BCASTS_FILE;
    if (strcmp(part, "conversations") == 0) return SD_CONVOS_FILE;
    if (strcmp(part, "chat_messages") == 0) return SD_DMS_FILE;
    if (strcmp(part, "fog_nodes") == 0) return SD_FOG_FILE;
    return nullptr;
}

static void mergePartToSD(const char* part, JsonDocument& newRecords) {
    const char* sdFile = partToSdFile(part);
    if (!sdFile) return;

    JsonArray incoming = newRecords.as<JsonArray>();
    if (incoming.size() == 0) return;

    // Read existing data from SD
    JsonDocument existing;
    readJsonArray(sdFile, existing);
    JsonArray arr = existing.is<JsonArray>() ? existing.as<JsonArray>()
                                              : existing.to<JsonArray>();

    int added = 0;
    for (JsonVariant item : incoming) {
        JsonObject rec = item.as<JsonObject>();
        int recId = rec["id"] | -1;

        // Check if this ID already exists (skip records without id)
        bool found = false;
        if (recId > 0) {
            for (JsonObject ex : arr) {
                if ((ex["id"] | -1) == recId) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            arr.add(rec);
            added++;
        }
    }

    if (added > 0) {
        writeJsonArray(sdFile, existing);
        logMsg('S', "SYNC_BACK: merged %d new %s records", added, part);
    }
}

static void handleSyncBack(JsonDocument& doc) {
    const char* part = doc["part"] | "";
    if (strlen(part) == 0) return;

    // Initialize buffers on first SYNC_BACK message
    if (!sbInProgress) {
        initSyncBackBuffers();
    }

    // Count message: merge accumulated records to SD
    if (doc["n"].is<int>()) {
        JsonDocument* buf = getSyncBackBuffer(part);
        if (buf) {
            mergePartToSD(part, *buf);
        }
        return;
    }

    // Single record: accumulate in buffer
    if (doc["d"].is<JsonObject>()) {
        JsonDocument* buf = getSyncBackBuffer(part);
        if (buf) {
            buf->as<JsonArray>().add(doc["d"].as<JsonObject>());
        }
    }
}

static void handleSyncBackSC(JsonDocument& doc) {
    // SC continuation for SYNC_BACK — same format as regular SC
    const char* part = doc["p"] | "";
    int seq = doc["s"] | -1;
    const char* key = doc["k"] | "";
    const char* val = doc["v"] | "";
    if (seq < 0 || strlen(key) == 0 || strlen(part) == 0) return;

    JsonDocument* buf = getSyncBackBuffer(part);
    if (!buf) return;

    JsonArray arr = buf->as<JsonArray>();
    if (seq >= (int)arr.size()) return;

    JsonObject rec = arr[seq].as<JsonObject>();
    rec[key] = rec[key].as<String>() + val;
}

static void handleSyncBackDone(const char* nodeId) {
    sbInProgress = false;
    logMsg('S', "SYNC_BACK complete from %s", nodeId);
}

// ── Message cleanup (48-hour TTL) ──────────────────────────────────
//
// Direct messages older than 48 hours are automatically deleted.
// This runs periodically in nodeProtocolLoop().

static unsigned long lastCleanupMs = 0;

static void cleanupOldMessages() {
    // Relative timestamp check: ESP32 doesn't have a synced real-time clock,
    // so we compare sent_at timestamps relative to the newest message.
    // Messages with sent_at > 0 and more than 48h older than the newest
    // message are deleted.
    JsonDocument doc;
    readJsonArray(SD_DMS_FILE, doc);
    if (!doc.is<JsonArray>()) return;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) return;

    // Find the latest timestamp
    long latestTs = 0;
    for (JsonObject m : arr) {
        long ts = m["sent_at"] | 0L;
        if (ts > latestTs) latestTs = ts;
    }

    if (latestTs == 0) return;  // No valid timestamps

    // Remove messages older than 48 hours (172800 seconds)
    long cutoff = latestTs - MESSAGE_TTL_SECONDS;
    int removed = 0;
    int i = 0;
    while (i < (int)arr.size()) {
        JsonObject m = arr[i].as<JsonObject>();
        long ts = m["sent_at"] | 0L;
        if (ts > 0 && ts < cutoff) {
            arr.remove(i);
            removed++;
        } else {
            i++;
        }
    }

    if (removed > 0) {
        writeJsonArray(SD_DMS_FILE, doc);
        logMsg('S', "Cleanup: removed %d messages older than 48h", removed);
    }
}

// ── Public API ─────────────────────────────────────────────────────

static unsigned long lastPingMs = 0;
static unsigned long lastAutoSyncMs = 0;
static int autoSyncNodeIdx = 0;  // Round-robin index for cycling through nodes

void nodeProtocolInit() {
    nodeCount = 0;
    memset(nodes, 0, sizeof(nodes));
    lastPingMs = 0;
    lastAutoSyncMs = 0;
    autoSyncNodeIdx = 0;
    lastCleanupMs = 0;
    syncState = SYNC_IDLE;
}

void nodeProtocolLoop() {
    unsigned long now = millis();

    // ── Process non-blocking sync state machine ────────────────────
    // Sends at most one record/chunk per call, then returns so
    // dnsServer + WiFi can process. No delay() calls.
    syncTick();

    // ── Admin sends periodic PING via BROADCAST (small, fits 72 B) ─
    if (now - lastPingMs >= ADMIN_PING_INTERVAL_MS) {
        JsonDocument ping;
        ping["cmd"] = "PING";
        ping["node_id"] = "admin";
        sendBroadcast(ping);
        lastPingMs = now;
    }

    // ── Auto-sync to active nodes (every 5 minutes, one node per cycle) ──
    // Cycles through nodes round-robin. Only starts a new sync if idle.
    if (now - lastAutoSyncMs >= AUTO_SYNC_INTERVAL_MS) {
        lastAutoSyncMs = now;
        if (syncState == SYNC_IDLE && nodeCount > 0) {
            // Find next active node starting from autoSyncNodeIdx
            for (int tries = 0; tries < nodeCount; tries++) {
                int idx = (autoSyncNodeIdx + tries) % nodeCount;
                if (nodes[idx].xbeeAddr.valid &&
                    now - nodes[idx].last_heartbeat <= NODE_STALE_MS) {
                    syncStart(nodes[idx].node_id, nodes[idx].xbeeAddr, false);
                    logMsg('S', "Auto-sync (incremental) to %s", nodes[idx].node_id);
                    autoSyncNodeIdx = (idx + 1) % nodeCount;
                    break;
                }
            }
        }
    }

    // ── Periodic message cleanup (every 10 minutes) ────────────────
    if (now - lastCleanupMs >= CLEANUP_INTERVAL_MS) {
        lastCleanupMs = now;
        cleanupOldMessages();
    }
}

bool nodeProtocolHandleLine(const char* line, size_t len,
                            const XBeeAddr& src) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line, len);
    if (err) return false;

    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    const char* nodeId = doc["node_id"] | "unknown";
    JsonObject params = doc["params"].as<JsonObject>();

    logMsg('S', "CMD=%s from %s", cmd, nodeId);

    if (strcmp(cmd, "REGISTER") == 0) {
        handleRegister(nodeId, params, src);
    } else if (strcmp(cmd, "HEARTBEAT") == 0) {
        handleHeartbeat(nodeId, params, src);
    } else if (strcmp(cmd, "SYNC_REQUEST") == 0) {
        // Start non-blocking sync (if not already syncing)
        if (syncState == SYNC_IDLE) {
            syncStart(nodeId, src, true);  // Node request = full sync
        } else {
            logMsg('S', "SYNC busy, ignoring request from %s", nodeId);
        }
    } else if (strcmp(cmd, "SOS_ALERT") == 0) {
        handleSosAlert(nodeId, params);
    } else if (strcmp(cmd, "CHANGE_PASSWORD") == 0) {
        handleChangePassword(params);
    } else if (strcmp(cmd, "RELAY_FOG_NODE") == 0) {
        handleRelayFogNode(params);
    } else if (strcmp(cmd, "RELAY_CHAT_MSG") == 0) {
        handleRelayChatMsg(nodeId, params);
    } else if (strcmp(cmd, "RELAY_MSG") == 0) {
        // Log only
    } else if (strcmp(cmd, "STATS_RESPONSE") == 0) {
        handleStatsResponse(nodeId, params);
    } else if (strcmp(cmd, "SYNC_BACK") == 0) {
        handleSyncBack(doc);
    } else if (strcmp(cmd, "SC") == 0) {
        // SC messages can be for regular sync OR sync-back
        // During SYNC_BACK, SC messages are handled by handleSyncBackSC
        if (sbInProgress) {
            handleSyncBackSC(doc);
        } else {
            logMsg('S', "SC outside SYNC_BACK (ignored)");
        }
    } else if (strcmp(cmd, "SYNC_BACK_DONE") == 0) {
        handleSyncBackDone(nodeId);
    } else if (strcmp(cmd, "PONG") == 0) {
        // Node responded to our PING
    } else if (strcmp(cmd, "PING") == 0) {
        // Node sent a PING — reply with PONG via unicast
        JsonDocument pong;
        pong["cmd"] = "PONG";
        pong["node_id"] = "admin";
        sendReply(src, pong);
    } else {
        return false;
    }
    return true;
}

void nodeProtocolGetNodes(JsonArray& arr) {
    unsigned long now = millis();
    for (int i = 0; i < nodeCount; i++) {
        NodeInfo& n = nodes[i];
        if (now - n.last_heartbeat > NODE_STALE_MS) {
            strncpy(n.status, "stale", sizeof(n.status) - 1);
        }
        JsonObject o = arr.add<JsonObject>();
        o["node_id"] = n.node_id;
        o["device_name"] = n.device_name;
        o["ip_address"] = n.ip_address;
        o["status"] = n.status;
        o["seconds_since_heartbeat"] = (int)((now - n.last_heartbeat) / 1000);
        o["free_heap"] = n.free_heap;
        o["uptime"] = n.uptime;
        o["xbee_addr_valid"] = n.xbeeAddr.valid;
        if (n.xbeeAddr.valid) {
            char addrStr[20];
            snprintf(addrStr, sizeof(addrStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
                     n.xbeeAddr.addr64[0], n.xbeeAddr.addr64[1],
                     n.xbeeAddr.addr64[2], n.xbeeAddr.addr64[3],
                     n.xbeeAddr.addr64[4], n.xbeeAddr.addr64[5],
                     n.xbeeAddr.addr64[6], n.xbeeAddr.addr64[7]);
            o["xbee_addr"] = String(addrStr);
        }
    }
}

int nodeProtocolActiveCount() {
    unsigned long now = millis();
    int count = 0;
    for (int i = 0; i < nodeCount; i++) {
        if (now - nodes[i].last_heartbeat <= NODE_STALE_MS) count++;
    }
    return count;
}

int nodeProtocolTotalCount() {
    return nodeCount;
}

void nodeProtocolTriggerSync(const char* nodeId) {
    // Start non-blocking sync (if not already syncing)
    if (syncState != SYNC_IDLE) {
        logMsg('S', "SYNC already in progress, ignoring trigger for %s", nodeId);
        return;
    }
    NodeInfo* n = findNode(nodeId);
    XBeeAddr dest = {};
    if (n && n->xbeeAddr.valid) {
        dest = n->xbeeAddr;
    } else {
        dest = xbeeGetLastSource();
    }
    syncStart(nodeId, dest, true);  // Manual trigger = full sync
}

bool nodeProtocolSyncInProgress() {
    return syncState != SYNC_IDLE;
}
