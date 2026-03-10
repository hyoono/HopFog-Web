/*
 * node_protocol.cpp — Handle HopFog-Node JSON commands over XBee.
 *
 * CRITICAL: ZigBee broadcast max payload is ~84 bytes. All RESPONSES
 * are sent via unicast (xbeeSendTo) which supports fragmentation up
 * to ~255 bytes. Only discovery PINGs use broadcast.
 *
 * SYNC_DATA is chunked into multiple small messages to stay within
 * the unicast payload limit.
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

static void handleSyncRequest(const char* nodeId, const XBeeAddr& dest) {
    // SYNC_DATA sends records ONE AT A TIME.  ALL fields are included
    // (password_hash, body, message_text — nothing is stripped).
    //
    // If a record fits in XBEE_UNICAST_MAX (240 B), it's sent as one message:
    //   {"cmd":"SYNC_DATA","node_id":"...","part":"users","seq":0,"d":{record}}
    //
    // If a record is TOO LARGE, long text fields (>30 chars) are emptied in
    // the base message, and the full text is sent in SC (Sync Continuation)
    // messages:
    //   {"cmd":"SC","p":"users","s":0,"k":"body","v":"...chunk of text..."}
    //
    // The node appends each SC chunk to the corresponding record+field.
    // Count message (no "d"): {"cmd":"SYNC_DATA",...,"part":"users","n":5}
    // Final: {"cmd":"SYNC_DONE","node_id":"..."}

    auto sendPart = [&](const char* partName, const char* sdFile) {
        JsonDocument fileDoc;
        readJsonArray(sdFile, fileDoc);
        JsonArray arr = fileDoc.is<JsonArray>() ? fileDoc.as<JsonArray>()
                                                 : fileDoc.to<JsonArray>();

        int sent = 0;
        for (JsonVariant item : arr) {
            JsonObject rec = item.as<JsonObject>();

            // Build SYNC_DATA with ALL fields (no stripping)
            JsonDocument msg;
            msg["cmd"] = "SYNC_DATA";
            msg["node_id"] = nodeId;
            msg["part"] = partName;
            msg["seq"] = sent;
            JsonObject d = msg["d"].to<JsonObject>();
            for (JsonPair kv : rec) {
                d[kv.key()] = kv.value();
            }

            // Check if it fits in unicast
            String json;
            serializeJson(msg, json);

            if ((int)json.length() <= XBEE_UNICAST_MAX) {
                // Fits! Send as-is.
                sendReply(dest, msg);
                sent++;
                delay(SYNC_CHUNK_DELAY_MS);
                continue;
            }

            // ── Record too large: chunk ALL string fields via SC ──
            // Strategy: send a skeleton base with only the "id" field
            // (guaranteed to fit), then send every field as SC messages.

            // Collect ALL fields for continuation
            String allKeys[16];
            String allValues[16];
            int nFields = 0;

            for (JsonPair kv : rec) {
                if (nFields >= 16) {
                    logMsg('E', "%s[%d] has >16 string fields, some skipped",
                           partName, sent);
                    break;
                }
                if (!kv.value().is<const char*>()) continue;
                allKeys[nFields] = kv.key().c_str();
                allValues[nFields] = kv.value().as<const char*>();
                nFields++;
            }

            // Build skeleton base: only non-string fields (id, ints, bools)
            JsonDocument skelMsg;
            skelMsg["cmd"] = "SYNC_DATA";
            skelMsg["node_id"] = nodeId;
            skelMsg["part"] = partName;
            skelMsg["seq"] = sent;
            JsonObject skelD = skelMsg["d"].to<JsonObject>();
            for (JsonPair kv : rec) {
                if (kv.value().is<const char*>()) {
                    skelD[kv.key()] = "";  // Empty all string fields
                } else {
                    skelD[kv.key()] = kv.value();  // Keep ints, bools
                }
            }
            sendReply(dest, skelMsg);
            delay(SYNC_CHUNK_DELAY_MS);

            // Send SC messages for each string field
            for (int i = 0; i < nFields; i++) {
                int offset = 0;
                int fullLen = allValues[i].length();
                if (fullLen == 0) continue;  // Skip empty strings

                while (offset < fullLen) {
                    JsonDocument cont;
                    cont["cmd"] = "SC";
                    cont["p"] = partName;
                    cont["s"] = sent;
                    cont["k"] = allKeys[i];

                    // Dynamic chunk sizing — reduce until serialized size fits
                    int trySize = 130;
                    int actualEnd = 0;
                    String cJson;
                    bool fits = false;
                    while (trySize >= 10) {
                        actualEnd = (offset + trySize < fullLen)
                                    ? offset + trySize : fullLen;
                        cont["v"] = allValues[i].substring(offset, actualEnd);
                        serializeJson(cont, cJson);
                        if ((int)cJson.length() <= XBEE_UNICAST_MAX) {
                            fits = true;
                            break;
                        }
                        trySize -= 10;
                    }

                    if (!fits) {
                        logMsg('E', "SC chunk too large %s[%d].%s",
                               partName, sent, allKeys[i].c_str());
                        break;
                    }

                    sendReply(dest, cont);
                    delay(SYNC_CHUNK_DELAY_MS);
                    offset = actualEnd;
                }
            }
            sent++;
        }

        // Count message for this part
        JsonDocument countMsg;
        countMsg["cmd"] = "SYNC_DATA";
        countMsg["node_id"] = nodeId;
        countMsg["part"] = partName;
        countMsg["n"] = sent;
        sendReply(dest, countMsg);
        delay(SYNC_CHUNK_DELAY_MS);

        logMsg('S', "Synced %s: %d records", partName, sent);
    };

    sendPart("users", SD_USERS_FILE);
    sendPart("announcements", SD_BCASTS_FILE);
    sendPart("conversations", SD_CONVOS_FILE);
    sendPart("chat_messages", SD_DMS_FILE);
    sendPart("fog_nodes", SD_FOG_FILE);

    // Final: SYNC_DONE
    JsonDocument done;
    done["cmd"] = "SYNC_DONE";
    done["node_id"] = nodeId;
    sendReply(dest, done);

    logMsg('S', "SYNC complete to %s", nodeId);
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

// ── SYNC_BACK: Node → Admin data merge ─────────────────────────────
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

// ── Public API ─────────────────────────────────────────────────────

static unsigned long lastPingMs = 0;

// ── Deferred sync — runs in loop() instead of blocking web handler ─
static char  pendingSyncNodeId[32] = "";
static XBeeAddr pendingSyncAddr    = {};
static bool  syncPending           = false;

void nodeProtocolInit() {
    nodeCount = 0;
    memset(nodes, 0, sizeof(nodes));
    lastPingMs = 0;
    syncPending = false;
}

void nodeProtocolLoop() {
    unsigned long now = millis();

    // ── Process deferred sync (set by web handler or XBee command) ──
    // Running here keeps the web handler responsive and prevents the
    // async web server from timing out (which caused "FS corruption").
    if (syncPending) {
        syncPending = false;
        handleSyncRequest(pendingSyncNodeId, pendingSyncAddr);
    }

    // Admin sends periodic PING via BROADCAST (small, fits in 72 bytes).
    // This is the only message that uses broadcast — for discovery.
    if (now - lastPingMs >= ADMIN_PING_INTERVAL_MS) {
        JsonDocument ping;
        ping["cmd"] = "PING";
        ping["node_id"] = "admin";
        sendBroadcast(ping);
        lastPingMs = now;
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
        // Defer sync to nodeProtocolLoop() — don't block XBee processing
        strncpy(pendingSyncNodeId, nodeId, sizeof(pendingSyncNodeId) - 1);
        pendingSyncNodeId[sizeof(pendingSyncNodeId) - 1] = '\0';
        pendingSyncAddr = src;
        syncPending = true;
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
    // Defer to nodeProtocolLoop() — prevents async web server timeout
    NodeInfo* n = findNode(nodeId);
    if (n && n->xbeeAddr.valid) {
        pendingSyncAddr = n->xbeeAddr;
    } else {
        pendingSyncAddr = xbeeGetLastSource();
    }
    strncpy(pendingSyncNodeId, nodeId, sizeof(pendingSyncNodeId) - 1);
    pendingSyncNodeId[sizeof(pendingSyncNodeId) - 1] = '\0';
    syncPending = true;
}
