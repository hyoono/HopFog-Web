/*
 * node_protocol.h — HopFog node protocol handler.
 *
 * Parses incoming JSON commands from HopFog-Node devices over XBee
 * and sends appropriate responses (REGISTER_ACK, PONG, SYNC_DATA, etc).
 *
 * CRITICAL: ZigBee broadcast max payload is ~84 bytes.
 * - Broadcast: only used for PING discovery (~35 bytes)
 * - Unicast:   used for all responses (supports fragmentation, ~255 bytes)
 * - SYNC_DATA: chunked into multiple small unicast messages
 */

#ifndef NODE_PROTOCOL_H
#define NODE_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "xbee_comm.h"   // for XBeeAddr

// ── Max registered nodes tracked in memory ─────────────────────────
#define MAX_NODES 10
#define NODE_STALE_MS 90000  // mark stale after 90 s without heartbeat

// ── Admin periodic timers ──────────────────────────────────────────
#define ADMIN_PING_INTERVAL_MS  10000  // send PING every 10 seconds
#define NODE_UNICAST_PACING_MS  50     // delay between unicast sends to avoid XBee overload

// ── Node info stored in memory ─────────────────────────────────────
struct NodeInfo {
    char   node_id[32];
    char   device_name[32];
    char   ip_address[16];
    char   status[10];    // "active" or "stale"
    unsigned long registered_at;
    unsigned long last_heartbeat;
    int    free_heap;
    int    uptime;
    XBeeAddr xbeeAddr;    // XBee 64-bit + 16-bit address for unicast
};

// ── Public API ─────────────────────────────────────────────────────

void nodeProtocolInit();
void nodeProtocolLoop();

/// Parse payload and dispatch. src = source XBee address for unicast replies.
bool nodeProtocolHandleLine(const char* line, size_t len,
                            const XBeeAddr& src);

void nodeProtocolGetNodes(JsonArray& arr);
int nodeProtocolActiveCount();
int nodeProtocolTotalCount();
void nodeProtocolTriggerSync(const char* nodeId);

/// Send a message to ALL registered nodes via unicast (supports ~240 bytes).
/// Falls back to broadcast if no nodes are registered.
void nodeProtocolSendToAllNodes(const char* json, size_t len);

#endif // NODE_PROTOCOL_H
