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
    // SYNC_DATA sends records ONE AT A TIME to stay within the
    // XBee unicast payload limit (240 bytes).
    //
    // Format: {"cmd":"SYNC_DATA","node_id":"...","part":"users","seq":0,"d":{...}}
    //         {"cmd":"SYNC_DATA","node_id":"...","part":"users","seq":1,"d":{...}}
    //         ...
    //         {"cmd":"SYNC_DATA","node_id":"...","part":"users","n":3}  (count msg)
    //         {"cmd":"SYNC_DONE","node_id":"..."}  (after all parts)
    //
    // "d" is used instead of "data" to save bytes.
    // "n" = total count for that part (sent as final message for each part).

    auto sendPart = [&](const char* partName, const char* sdFile) {
        JsonDocument fileDoc;
        readJsonArray(sdFile, fileDoc);
        JsonArray arr = fileDoc.is<JsonArray>() ? fileDoc.as<JsonArray>()
                                                 : fileDoc.to<JsonArray>();

        int sent = 0;
        int total = 0;
        for (JsonVariant item : arr) {
            JsonDocument msg;
            msg["cmd"] = "SYNC_DATA";
            msg["node_id"] = nodeId;
            msg["part"] = partName;
            msg["seq"] = total;

            // Copy the record, stripping large fields to fit in 240 bytes
            JsonObject rec = item.as<JsonObject>();
            JsonObject d = msg["d"].to<JsonObject>();
            for (JsonPair kv : rec) {
                const char* key = kv.key().c_str();
                // Skip password hashes (not needed on node)
                if (strcmp(key, "password_hash") == 0) continue;
                // Truncate long text fields (body, text, content)
                if ((strcmp(key, "body") == 0 || strcmp(key, "text") == 0 ||
                     strcmp(key, "content") == 0) && kv.value().is<const char*>()) {
                    const char* val = kv.value().as<const char*>();
                    if (strlen(val) > 60) {
                        char trunc[61];
                        strncpy(trunc, val, 60);
                        trunc[60] = '\0';
                        d[key] = trunc;
                    } else {
                        d[key] = val;
                    }
                } else {
                    d[key] = kv.value();
                }
            }

            String json;
            serializeJson(msg, json);
            total++;
            if (json.length() <= XBEE_UNICAST_MAX) {
                sendReply(dest, msg);
                sent++;
            } else {
                logMsg('E', "Record too large for %s[%d]: %d B",
                       partName, total - 1, (int)json.length());
            }
            delay(SYNC_CHUNK_DELAY_MS);
        }

        // Send count message for this part
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

// ── Public API ─────────────────────────────────────────────────────

static unsigned long lastPingMs = 0;

void nodeProtocolInit() {
    nodeCount = 0;
    memset(nodes, 0, sizeof(nodes));
    lastPingMs = 0;
}

void nodeProtocolLoop() {
    unsigned long now = millis();

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
        handleSyncRequest(nodeId, src);
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
    // Find node's XBee address for unicast
    NodeInfo* n = findNode(nodeId);
    XBeeAddr dest = {};
    if (n && n->xbeeAddr.valid) {
        dest = n->xbeeAddr;
    } else {
        // Fallback: use last received source address
        dest = xbeeGetLastSource();
    }
    handleSyncRequest(nodeId, dest);
}
