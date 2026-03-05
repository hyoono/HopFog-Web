/*
 * node_protocol.h — HopFog node protocol handler.
 *
 * Parses incoming JSON commands from HopFog-Node devices over XBee
 * and sends appropriate responses (REGISTER_ACK, PONG, SYNC_DATA, etc).
 *
 * Protocol: newline-delimited JSON over XBee.  Every message has at
 * minimum:  {"cmd":"COMMAND","node_id":"node-01","ts":12345}
 */

#ifndef NODE_PROTOCOL_H
#define NODE_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Max registered nodes tracked in memory ─────────────────────────
#define MAX_NODES 10
#define NODE_STALE_MS 90000  // mark stale after 90 s without heartbeat

// ── Node info stored in memory ─────────────────────────────────────
struct NodeInfo {
    char   node_id[32];
    char   device_name[32];
    char   ip_address[16];
    char   xbee_addr[20]; // "XXXXXXXXXXXXXXXX"
    char   status[10];    // "active" or "stale"
    unsigned long registered_at;  // millis()
    unsigned long last_heartbeat; // millis()
    int    free_heap;
    int    uptime;
};

// ── Public API ─────────────────────────────────────────────────────

/// Initialise the node registry (call once in setup).
void nodeProtocolInit();

/// Try to parse a line as a JSON node command and dispatch it.
/// Called from the XBee receive callback (AT/transparent mode).
/// Returns true if the line was a valid JSON command and was handled.
bool nodeProtocolHandleLine(const char* line, size_t len);

/// Get all registered nodes as a JSON array (for /api/nodes).
void nodeProtocolGetNodes(JsonArray& arr);

/// Get count of active (non-stale) nodes.
int nodeProtocolActiveCount();

/// Get total registered nodes.
int nodeProtocolTotalCount();

/// Trigger a SYNC_DATA send to a specific node by ID.
void nodeProtocolTriggerSync(const char* nodeId);

#endif // NODE_PROTOCOL_H
