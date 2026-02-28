/*
 * node_protocol.cpp — Handle HopFog-Node JSON commands over XBee.
 *
 * When a node sends {"cmd":"REGISTER","node_id":"node-01",...} the admin
 * parses it, stores the node info, and replies with {"cmd":"REGISTER_ACK",...}.
 *
 * Supported incoming commands:
 *   REGISTER, HEARTBEAT, SYNC_REQUEST, RELAY_MSG, RELAY_FOG_NODE,
 *   RELAY_CHAT_MSG, SOS_ALERT, CHANGE_PASSWORD, STATS_RESPONSE
 *
 * Outgoing responses:
 *   REGISTER_ACK, PONG, SYNC_DATA, BROADCAST_MSG, GET_STATS
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

static NodeInfo* registerNode(const char* nodeId) {
    NodeInfo* n = findNode(nodeId);
    if (n) return n;
    if (nodeCount >= MAX_NODES) return nullptr; // registry full
    n = &nodes[nodeCount++];
    memset(n, 0, sizeof(NodeInfo));
    strncpy(n->node_id, nodeId, sizeof(n->node_id) - 1);
    strncpy(n->status, "active", sizeof(n->status) - 1);
    n->registered_at = millis();
    n->last_heartbeat = millis();
    return n;
}

// ── Helper: send a JSON command via XBee broadcast ─────────────────
static void sendJsonCommand(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Command handlers ───────────────────────────────────────────────

static void handleRegister(const char* nodeId, JsonObject params,
                           uint32_t sHi, uint32_t sLo) {
    NodeInfo* n = registerNode(nodeId);
    if (!n) {
        Serial.println("[NODE] Registry full, cannot register");
        return;
    }
    strncpy(n->device_name,
            params["device_name"] | nodeId,
            sizeof(n->device_name) - 1);
    strncpy(n->ip_address,
            params["ip_address"] | "",
            sizeof(n->ip_address) - 1);
    strncpy(n->status, "active", sizeof(n->status) - 1);
    n->last_heartbeat = millis();
    snprintf(n->xbee_addr, sizeof(n->xbee_addr),
             "%08X%08X", sHi, sLo);

    // Reply REGISTER_ACK
    JsonDocument ack;
    ack["cmd"] = "REGISTER_ACK";
    ack["node_id"] = nodeId;
    sendJsonCommand(ack);
    Serial.printf("[NODE] Registered %s (%s)\n", nodeId, n->ip_address);
}

static void handleHeartbeat(const char* nodeId, JsonObject params,
                            uint32_t sHi, uint32_t sLo) {
    NodeInfo* n = findNode(nodeId);
    if (!n) n = registerNode(nodeId);
    if (!n) return;
    n->last_heartbeat = millis();
    strncpy(n->status, "active", sizeof(n->status) - 1);
    strncpy(n->ip_address,
            params["ip_address"] | n->ip_address,
            sizeof(n->ip_address) - 1);
    n->free_heap = params["free_heap"] | 0;
    n->uptime = params["uptime"] | 0;

    // Reply PONG
    JsonDocument pong;
    pong["cmd"] = "PONG";
    pong["node_id"] = nodeId;
    sendJsonCommand(pong);
}

static void handleSyncRequest(const char* nodeId) {
    // Build SYNC_DATA from SD card files
    JsonDocument sync;
    sync["cmd"] = "SYNC_DATA";
    sync["node_id"] = nodeId;

    // Users
    JsonDocument usersDoc;
    readJsonArray(SD_USERS_FILE, usersDoc);
    JsonArray usersArr = sync["users"].to<JsonArray>();
    if (usersDoc.is<JsonArray>()) {
        for (JsonObject u : usersDoc.as<JsonArray>()) {
            if (u["is_active"].as<int>() == 1) {
                JsonObject o = usersArr.add<JsonObject>();
                o["id"] = u["id"];
                o["username"] = u["username"];
                o["email"] = u["email"];
                o["role"] = u["role"];
                o["is_active"] = true;
                o["has_agreed_sos"] = (u["has_agreed_sos"].as<int>() == 1);
            }
        }
    }

    // Announcements (sent broadcasts)
    JsonDocument bcastDoc;
    readJsonArray(SD_BCASTS_FILE, bcastDoc);
    JsonArray annArr = sync["announcements"].to<JsonArray>();
    if (bcastDoc.is<JsonArray>()) {
        int count = 0;
        for (JsonObject b : bcastDoc.as<JsonArray>()) {
            const char* st = b["status"] | "";
            if (strcmp(st, "sent") == 0 || strcmp(st, "queued") == 0) {
                JsonObject o = annArr.add<JsonObject>();
                o["id"] = b["id"];
                o["title"] = b["subject"];
                o["message"] = b["body"];
                o["created_at"] = b["created_at"];
                if (++count >= 50) break;
            }
        }
    }

    // Conversations
    JsonDocument convDoc;
    readJsonArray(SD_CONVOS_FILE, convDoc);
    sync["conversations"] = convDoc.as<JsonArray>();

    // Chat messages (direct messages, last 100)
    JsonDocument dmDoc;
    readJsonArray(SD_DMS_FILE, dmDoc);
    JsonArray chatArr = sync["chat_messages"].to<JsonArray>();
    if (dmDoc.is<JsonArray>()) {
        JsonArray dmArr = dmDoc.as<JsonArray>();
        int start = (int)dmArr.size() - 100;
        if (start < 0) start = 0;
        for (int i = start; i < (int)dmArr.size(); i++) {
            chatArr.add(dmArr[i]);
        }
    }

    // Fog nodes
    JsonDocument fogDoc;
    readJsonArray(SD_FOG_FILE, fogDoc);
    sync["fog_nodes"] = fogDoc.as<JsonArray>();

    // Messages (log)
    sync["messages"] = JsonArray();

    sendJsonCommand(sync);
    Serial.printf("[NODE] Sent SYNC_DATA to %s\n", nodeId);
}

static void handleSosAlert(const char* nodeId, JsonObject params) {
    int userId = params["user_id"] | 0;
    if (userId <= 0) return;

    // Create a resident_admin_message for the SOS
    JsonDocument doc;
    readJsonArray(SD_RES_MSG_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject m : arr) {
        int id = m["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    // Look up username
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
    Serial.printf("[NODE] SOS alert from user %d via %s\n", userId, nodeId);

    // Also send SOS via XBee to other nodes
    String xbeeMsg = "SOS_ALERT|" + username + "|SOS via " + String(nodeId);
    xbeeSendBroadcast(xbeeMsg.c_str(), xbeeMsg.length());
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
            u["password_hash"] = newPw;  // stored as plaintext on ESP32
            writeJsonArray(SD_USERS_FILE, doc);
            Serial.printf("[NODE] Password changed for user %d\n", userId);
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

    // Check if already exists
    for (JsonObject d : arr) {
        if (strcmp(d["device_name"] | "", name) == 0) {
            d["status"] = params["status"] | "active";
            writeJsonArray(SD_FOG_FILE, doc);
            return;
        }
    }

    // Add new
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
    Serial.printf("[NODE] Chat msg from node %s: conv=%d sender=%d\n",
                  nodeId, convId, senderId);
}

static void handleStatsResponse(const char* nodeId, JsonObject params) {
    NodeInfo* n = findNode(nodeId);
    if (!n) return;
    n->free_heap = params["free_heap"] | 0;
    n->uptime = params["uptime"] | 0;
    Serial.printf("[NODE] Stats from %s: heap=%d uptime=%d\n",
                  nodeId, n->free_heap, n->uptime);
}

// ── Public API ─────────────────────────────────────────────────────

void nodeProtocolInit() {
    nodeCount = 0;
    memset(nodes, 0, sizeof(nodes));
}

bool nodeProtocolHandleData(const uint8_t* data, size_t len,
                            uint32_t senderHi, uint32_t senderLo) {
    // Try to parse as JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) return false;

    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    const char* nodeId = doc["node_id"] | "unknown";
    JsonObject params = doc["params"].as<JsonObject>();

    Serial.printf("[NODE] CMD=%s from %s\n", cmd, nodeId);

    if (strcmp(cmd, "REGISTER") == 0) {
        handleRegister(nodeId, params, senderHi, senderLo);
    } else if (strcmp(cmd, "HEARTBEAT") == 0) {
        handleHeartbeat(nodeId, params, senderHi, senderLo);
    } else if (strcmp(cmd, "SYNC_REQUEST") == 0) {
        handleSyncRequest(nodeId);
    } else if (strcmp(cmd, "SOS_ALERT") == 0) {
        handleSosAlert(nodeId, params);
    } else if (strcmp(cmd, "CHANGE_PASSWORD") == 0) {
        handleChangePassword(params);
    } else if (strcmp(cmd, "RELAY_FOG_NODE") == 0) {
        handleRelayFogNode(params);
    } else if (strcmp(cmd, "RELAY_CHAT_MSG") == 0) {
        handleRelayChatMsg(nodeId, params);
    } else if (strcmp(cmd, "RELAY_MSG") == 0) {
        Serial.printf("[NODE] Relay msg from %s: %s -> %s\n",
                      nodeId,
                      (const char*)(params["from"] | "?"),
                      (const char*)(params["to"] | "?"));
    } else if (strcmp(cmd, "STATS_RESPONSE") == 0) {
        handleStatsResponse(nodeId, params);
    } else {
        Serial.printf("[NODE] Unknown command: %s\n", cmd);
        return false;
    }
    return true;
}

void nodeProtocolGetNodes(JsonArray& arr) {
    unsigned long now = millis();
    for (int i = 0; i < nodeCount; i++) {
        NodeInfo& n = nodes[i];
        // Mark stale if no heartbeat
        if (now - n.last_heartbeat > NODE_STALE_MS) {
            strncpy(n.status, "stale", sizeof(n.status) - 1);
        }
        JsonObject o = arr.add<JsonObject>();
        o["node_id"] = n.node_id;
        o["device_name"] = n.device_name;
        o["ip_address"] = n.ip_address;
        o["xbee_addr"] = n.xbee_addr;
        o["status"] = n.status;
        o["seconds_since_heartbeat"] = (int)((now - n.last_heartbeat) / 1000);
        o["free_heap"] = n.free_heap;
        o["uptime"] = n.uptime;
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
