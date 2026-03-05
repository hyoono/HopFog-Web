# Notes for HopFog-Node Copilot Agent

> **Context:** These notes are for the Copilot agent working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node) branch `copilot/add-xbee-node-functionality`. The admin repo ([hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)) uses **XBee API mode 1** (binary-framed packets). The node must be updated to also use API mode 1.

---

## What the Node Must Change

The node currently uses **AT/transparent mode** (`Serial.println(json)` to send, read-until-newline to receive). This is **incompatible** with the admin which uses API mode 1 (binary API frames with start delimiter `0x7E`, length, frame type, checksum).

**The node must be switched from AT mode to API mode 1.**

### Summary of Required Changes

| Area | Current (AT mode) | Required (API mode 1) |
|------|-------------------|----------------------|
| **XBee config (XCTU)** | AP=0, DH=0, DL=FFFF | **AP=1** |
| **How node sends** | `Serial.println(json)` | Build 0x10 Transmit Request frame |
| **How node receives** | Read bytes until `\n` | Parse 0x7E API frames, extract from 0x90 |
| **Frame structure** | Raw text with newline delimiter | Binary: `0x7E \| Len \| FrameType \| Data... \| Checksum` |

---

## XBee Module Configuration (All Modules)

| Parameter | Admin XBee | Node XBee | Description |
|-----------|-----------|-----------|-------------|
| **AP** | `1` (API mode 1) | `1` (API mode 1) | Binary framed packets |
| **CE** | `1` (Coordinator) | `0` (Router) | One coordinator, multiple routers |
| **ID** | `1234` | `1234` | Same PAN ID on all devices |
| **BD** | `3` (9600) | `3` (9600) | Baud rate |

> **Note:** In API mode 1, DH/DL are NOT needed — the destination address is specified in each Transmit Request frame header.

---

## Implementation Guide: API Mode 1 for ESP32 Node

### Step 1: Create XBee API Frame Builder/Parser

Add these functions to the node (new file or in existing XBee code):

```cpp
// ── Constants ──────────────────────────────────────────────────────
#define XBEE_START_DELIM  0x7E
#define XBEE_TX_REQUEST   0x10   // Transmit Request frame type
#define XBEE_RX_PACKET    0x90   // Receive Packet frame type
#define XBEE_TX_STATUS    0x8B   // Transmit Status frame type
#define XBEE_MAX_FRAME    512    // max frame data buffer

// ── Send a JSON payload as a broadcast ────────────────────────────
// Builds a 0x10 Transmit Request frame with 64-bit dest = 0x000000000000FFFF
//
// Frame structure:
//   0x7E | LenHi LenLo | 0x10 FrameID Dest64[8] Dest16[2] Radius Options | payload | Checksum
//
// Frame data = 14 header bytes + payload length
//
static uint8_t frameIdCounter = 0;

uint8_t xbeeSendBroadcast(HardwareSerial& xbeeSerial, const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST,            // [0]  frame type
        fid,                        // [1]  frame ID
        0x00, 0x00, 0x00, 0x00,     // [2-5]  64-bit dest high
        0x00, 0x00, 0xFF, 0xFF,     // [6-9]  64-bit dest low (broadcast)
        0xFF, 0xFE,                 // [10-11] 16-bit dest (broadcast)
        0x00,                       // [12] broadcast radius
        0x00                        // [13] options
    };

    // Checksum = 0xFF - (sum of all frame data bytes)
    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Write frame
    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));   // length MSB
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF)); // length LSB
    xbeeSerial.write(hdr, 14);                        // frame header
    xbeeSerial.write((const uint8_t*)payload, len);   // RF data
    xbeeSerial.write(cksum);                          // checksum
    xbeeSerial.flush();

    return fid;
}
```

### Step 2: Replace AT-mode Send Calls

Every place in the node code that currently does:

```cpp
// OLD — AT mode (REMOVE THIS):
xbeeSerial.println(json);
```

Must change to:

```cpp
// NEW — API mode 1:
String json = /* your JSON */;
xbeeSendBroadcast(xbeeSerial, json.c_str(), json.length());
```

### Step 3: Add API Frame Receive Parser

Replace the current `readStringUntil('\n')` approach with a state machine that parses API frames:

```cpp
// ── Frame receive state machine ─────────────────────────────────────
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };

static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// Call this from loop() instead of Serial.readStringUntil('\n'):
void xbeeProcessIncoming(HardwareSerial& xbeeSerial) {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = GOT_LEN_HI;
            break;

        case GOT_LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = GOT_LEN_LO;
            break;

        case GOT_LEN_LO:
            rxFrameLen |= b;
            rxIdx = 0;
            rxChecksum = 0;
            rxState = (rxFrameLen > 0 && rxFrameLen <= XBEE_MAX_FRAME)
                      ? READING_DATA : WAIT_DELIM;
            break;

        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = GOT_CHECKSUM;
            break;

        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                // Valid frame!
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen > 12) {
                    // 0x90 Receive Packet:
                    //   [0]     0x90
                    //   [1-8]   64-bit source address
                    //   [9-10]  16-bit source address
                    //   [11]    receive options
                    //   [12..]  RF data (the JSON payload)
                    const char* rfData = (const char*)&rxFrame[12];
                    size_t rfLen = rxFrameLen - 12;

                    // Strip trailing newline if present
                    while (rfLen > 0 && (rfData[rfLen-1] == '\n' || rfData[rfLen-1] == '\r'))
                        rfLen--;

                    if (rfLen > 0) {
                        // Null-terminate and pass to your existing JSON handler
                        rxFrame[12 + rfLen] = '\0';
                        handleXBeeData(String(rfData));  // your existing handler
                    }
                }
                else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    // 0x8B Transmit Status — check delivery result
                    uint8_t delivery = rxFrame[5];
                    if (delivery != 0) {
                        Serial.printf("[XBee] TX delivery failed (0x%02X)\n", delivery);
                    }
                }
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
```

### Step 4: Update loop()

```cpp
void loop() {
    // ... existing code ...

    // OLD — AT mode (REMOVE):
    // if (xbeeSerial.available()) {
    //     String data = xbeeSerial.readStringUntil('\n');
    //     handleXBeeData(data);
    // }

    // NEW — API mode 1:
    xbeeProcessIncoming(xbeeSerial);

    // ... rest of loop ...
}
```

### Step 5: Reconfigure XBee Module in XCTU

Change the node's XBee module setting:

| Parameter | Old Value | New Value |
|-----------|-----------|-----------|
| **AP** | `0` (Transparent) | **`1` (API enabled)** |

All other settings stay the same (CE=0, ID=1234, BD=9600).

---

## Protocol — No Changes Needed

The JSON command protocol stays exactly the same. Only the serial framing changes:

| Command | Direction | JSON Payload (inside API frame) |
|---------|-----------|------|
| `REGISTER` | Node → Admin | `{"cmd":"REGISTER","node_id":"node-01","ts":12345,"params":{"device_name":"Node 1","ip_address":"192.168.4.1"}}` |
| `REGISTER_ACK` | Admin → Node | `{"cmd":"REGISTER_ACK","node_id":"node-01"}` |
| `HEARTBEAT` | Node → Admin | `{"cmd":"HEARTBEAT","node_id":"node-01","params":{"uptime":300,"free_heap":45000}}` |
| `PONG` | Admin → Node | `{"cmd":"PONG","node_id":"node-01"}` |
| `SYNC_REQUEST` | Node → Admin | `{"cmd":"SYNC_REQUEST","node_id":"node-01"}` |
| `SYNC_DATA` | Admin → Node | `{"cmd":"SYNC_DATA","node_id":"node-01","users":[...],"announcements":[...]}` |
| `BROADCAST_MSG` | Admin → Node | `{"cmd":"BROADCAST_MSG","params":{"from":"admin","to":"all","message":"..."}}` |
| `RELAY_CHAT_MSG` | Node → Admin | `{"cmd":"RELAY_CHAT_MSG","node_id":"node-01","params":{"conversation_id":1,"sender_id":2,"message_text":"Hello"}}` |
| `SOS_ALERT` | Node → Admin | `{"cmd":"SOS_ALERT","node_id":"node-01","params":{"user_id":3}}` |
| `CHANGE_PASSWORD` | Node → Admin | `{"cmd":"CHANGE_PASSWORD","node_id":"node-01","params":{"user_id":2,"new_password":"newpw"}}` |
| `GET_STATS` | Admin → Node | `{"cmd":"GET_STATS","node_id":"node-01"}` |
| `STATS_RESPONSE` | Node → Admin | `{"cmd":"STATS_RESPONSE","node_id":"node-01","params":{"free_heap":45000,"uptime":300}}` |

---

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        ADMIN ESP32                          │
│                                                             │
│  Web UI (browser) ──HTTP──► API Handlers ──► SD Card JSON   │
│                                   │                         │
│                                   ▼                         │
│                        xbeeSendBroadcast()                  │
│                                   │                         │
│                     Build 0x10 TX Request frame              │
│                    (0x7E | Len | 0x10 | hdr | json | cksum) │
│                                   │                         │
│                              UART2 TX                       │
└───────────────────────────────────┼─────────────────────────┘
                                    │
                              XBee S2C Pro
                            (AP=1, CE=1, ID=1234)
                                    │
                              ~~~~ RF ~~~~
                                    │
                              XBee S2C
                            (AP=1, CE=0, ID=1234)
                                    │
┌───────────────────────────────────┼─────────────────────────┐
│                              UART RX                        │
│                                   │                         │
│                    Parse 0x90 RX Packet frame                │
│                    Extract RF data (JSON payload)            │
│                                   │                         │
│                     handleXBeeData(json)                    │
│                                   │                         │
│                     Parse JSON, dispatch command             │
│                                                             │
│                         NODE ESP32                           │
│  Mobile App ──HTTP──► Node API ──► Local Storage            │
└─────────────────────────────────────────────────────────────┘
```

---

## SYNC_DATA Payload Schema (what the admin sends)

```json
{
  "cmd": "SYNC_DATA",
  "node_id": "node-01",
  "users": [
    {"id": 1, "username": "admin", "email": "admin@hopfog.com", "role": "admin", "is_active": true, "has_agreed_sos": true}
  ],
  "announcements": [
    {"id": 1, "title": "Test", "message": "Test announcement", "created_at": "1709000000"}
  ],
  "conversations": [
    {"id": 1, "user1_id": 1, "user2_id": 2, "is_sos": false, "created_at": 1709000000}
  ],
  "chat_messages": [
    {"id": 1, "conversation_id": 1, "sender_id": 2, "message_text": "Hello", "sent_at": 1709000000}
  ],
  "fog_nodes": [
    {"id": 1, "device_name": "node-01", "status": "active"}
  ],
  "messages": []
}
```

> **Important for large SYNC_DATA:** XBee API mode 1 has a maximum RF payload size per frame, determined by the NP (Maximum Packet Payload Bytes) parameter — typically **72-256 bytes** depending on encryption and addressing overhead. Frames with payloads exceeding NP will be **rejected** by the XBee module (not fragmented). The admin limits SYNC_DATA to 50 announcements and 100 chat messages and uses compact JSON (`separators=(",",":")`) to stay within limits. If SYNC_DATA still exceeds the max payload, increase the node's `JsonDocument` size and consider having the admin send smaller chunked syncs (e.g., users in one frame, announcements in another).

---

## ArduinoJson v7 Note

If using ArduinoJson v7+, `doc.containsKey("key")` is deprecated. Use:

```cpp
// Old (deprecated in v7):
if (doc.containsKey("users")) { ... }

// New (v7+):
if (doc["users"].is<JsonArray>()) { ... }
```

---

## Checklist for the Node Agent

1. [ ] **Create `xbeeSendBroadcast()` function** — builds 0x10 Transmit Request frame with checksum
2. [ ] **Create `xbeeProcessIncoming()` state machine** — parses 0x7E API frames byte-by-byte
3. [ ] **Replace all `xbeeSerial.println(json)`** with `xbeeSendBroadcast(xbeeSerial, json.c_str(), json.length())`
4. [ ] **Replace `readStringUntil('\n')`** in loop() with `xbeeProcessIncoming(xbeeSerial)` call
5. [ ] **Handle 0x90 Receive Packet** — extract RF data starting at byte [12], pass to existing `handleXBeeData()`
6. [ ] **Handle 0x8B Transmit Status** — log delivery failures for debugging
7. [ ] **Reconfigure XBee module** — change AP from 0 to 1 in XCTU
8. [ ] **Test** — verify REGISTER → REGISTER_ACK round-trip with admin
9. [ ] **Test** — verify SYNC_REQUEST → SYNC_DATA response
10. [ ] **Test** — verify HEARTBEAT → PONG response

---

## Summary

**The JSON protocol stays exactly the same. Only the serial transport changes from raw text to binary API frames.** The node needs:
1. A function to build 0x10 TX Request frames (send)
2. A state machine to parse 0x90 RX Packet frames (receive)
3. XBee module reconfigured to AP=1 in XCTU
