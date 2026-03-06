# HopFog-Node — Build From Scratch Instructions

> **For:** Copilot agent (Claude Opus 4.6) working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
>
> **What this is:** Complete instructions to build a HopFog fog-computing node from scratch on ESP32-CAM. The node connects to the admin (HopFog-Web) via XBee S2C using API mode 1 binary frames and JSON payloads.
>
> **Admin repo:** [hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web) — fully working, tested, confirmed RX/TX functional.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Hardware & Wiring](#2-hardware--wiring)
3. [XBee Module Configuration (XCTU)](#3-xbee-module-configuration-xctu)
4. [PlatformIO Project Setup](#4-platformio-project-setup)
5. [File Structure](#5-file-structure)
6. [Implementation: config.h](#6-implementation-configh)
7. [Implementation: xbee_comm.h](#7-implementation-xbee_commh)
8. [Implementation: xbee_comm.cpp](#8-implementation-xbee_commcpp)
9. [Implementation: node_client.h](#9-implementation-node_clienth)
10. [Implementation: node_client.cpp](#10-implementation-node_clientcpp)
11. [Implementation: sd_storage.h/cpp](#11-implementation-sd_storageh-cpp)
12. [Implementation: web_server & API handlers](#12-implementation-web-server--api-handlers)
13. [Implementation: main.cpp](#13-implementation-maincpp)
14. [Protocol Reference](#14-protocol-reference)
15. [CRITICAL: GPIO Fix for ESP32-CAM](#15-critical-gpio-fix-for-esp32-cam)
16. [Debugging Guide](#16-debugging-guide)
17. [Implementation Checklist](#17-implementation-checklist)

---

## 1. Architecture Overview

```
┌────────────────────────────────────────────────────────┐
│                    ADMIN (HopFog-Web)                   │
│  ESP32-CAM  ──  XBee S2C Pro (Coordinator, AP=1, CE=1) │
│  WiFi AP "HopFog-Network"                              │
│  Serves admin dashboard + mobile app API               │
│  SD card: users, broadcasts, messages, conversations   │
└──────────────────────┬─────────────────────────────────┘
                       │ XBee ZigBee RF
                       │ (API mode 1 binary frames)
                       │ (JSON payloads inside frames)
┌──────────────────────┴─────────────────────────────────┐
│                    NODE (HopFog-Node)                    │
│  ESP32-CAM  ──  XBee S2C (Router, AP=1, CE=0)          │
│  WiFi AP "HopFog-Node-XX"                              │
│  Serves mobile app API (same endpoints as admin)       │
│  SD card: synced copy of users, broadcasts, messages   │
│  Syncs data from admin via SYNC_REQUEST/SYNC_DATA      │
│  Relays mobile user actions to admin via XBee           │
└────────────────────────────────────────────────────────┘
```

**How it works:**
1. Node boots, sends `REGISTER` to admin via XBee every 10 seconds
2. Admin replies `REGISTER_ACK`
3. Node sends `SYNC_REQUEST` to get all data (users, announcements, conversations, etc.)
4. Admin replies `SYNC_DATA` with full database dump
5. Node stores synced data on its SD card
6. Node creates its own WiFi AP for local mobile phones
7. Mobile phones connect to node's WiFi, use same API endpoints as admin
8. Node relays user actions (send message, SOS, etc.) to admin via XBee
9. Node sends `HEARTBEAT` every 30 seconds; admin replies `PONG`

---

## 2. Hardware & Wiring

### Components
- AI-Thinker ESP32-CAM (with built-in micro SD card slot)
- Digi XBee S2C or S2C Pro module
- XBee breakout board (for breadboard mounting)
- Micro SD card (FAT32 formatted)
- USB-to-serial programmer (FTDI or CP2102) for flashing

### Wiring: ESP32-CAM ↔ XBee

```
ESP32-CAM          XBee Module
─────────          ───────────
GPIO 13 (TX) ────► DIN  (pin 3)
GPIO 12 (RX) ◄──── DOUT (pin 2)
3.3V         ────► VCC  (pin 1)
GND          ────► GND  (pin 10)
```

**⚠️ GPIO 12 boot-strapping note:** GPIO 12 controls the flash voltage at boot. If the ESP32 fails to boot with XBee connected, either:
- Disconnect XBee DOUT from GPIO 12 during power-on, reconnect after boot
- Or burn the VDD_SDIO efuse to force 3.3V (permanent, one-time): `espefuse.py set_flash_voltage 3.3V`

### SD Card
The ESP32-CAM's built-in SD card slot is used in 1-bit SD_MMC mode. No external wiring needed — just insert a FAT32-formatted micro SD card.

---

## 3. XBee Module Configuration (XCTU)

Use Digi's XCTU software to configure the XBee module **before** connecting it to the ESP32.

| Parameter | Value | Description |
|-----------|-------|-------------|
| **AP** | `1` | API mode 1 (binary frames, no escaping) |
| **CE** | `0` | Router (joins coordinator's network) |
| **ID** | `1234` | PAN ID — **must match the admin XBee** |
| **BD** | `3` | 9600 baud |
| **JV** | `1` | Join verification (router joins on power-up) |
| **DH** | `0` | Destination address high (broadcast) |
| **DL** | `FFFF` | Destination address low (broadcast) |

The **admin XBee** is configured as:
| Parameter | Value |
|-----------|-------|
| **AP** | `1` |
| **CE** | `1` (Coordinator) |
| **ID** | `1234` (same PAN ID) |
| **BD** | `3` (9600 baud) |

**After writing settings, power-cycle the XBee before connecting to ESP32!**

### How to verify XBee association
In XCTU, read the `AI` (Association Indication) parameter:
- `0x00` = Successfully joined coordinator's network ✅
- `0x21` = Scan found no PANs (coordinator not powered?)
- `0x22` = Not found valid PAN (wrong PAN ID?)
- `0x23` = Join failed (authentication?)

---

## 4. PlatformIO Project Setup

Create `platformio.ini` in the project root:

```ini
; PlatformIO configuration for HopFog-Node
; Build:   pio run
; Flash:   pio run --target upload
; Monitor: pio device monitor

[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino
monitor_speed = 115200
lib_deps =
    me-no-dev/ESPAsyncWebServer@^1.2.4
    me-no-dev/AsyncTCP@^1.1.1
    bblanchon/ArduinoJson@^7.3.0
board_build.partitions = min_spiffs.csv
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM=1
    -DUSE_SD_MMC=1
```

---

## 5. File Structure

```
HopFog-Node/
├── platformio.ini
├── include/
│   ├── config.h            # WiFi, pins, SD paths, constants
│   ├── xbee_comm.h         # XBee API mode 1 driver header
│   ├── node_client.h       # Node protocol client header
│   └── sd_storage.h        # SD card read/write header
├── src/
│   ├── main.cpp            # setup() + loop()
│   ├── xbee_comm.cpp       # XBee API mode 1 driver
│   ├── node_client.cpp     # Node protocol client (REGISTER, HEARTBEAT, SYNC)
│   ├── sd_storage.cpp      # SD card init + JSON file read/write
│   ├── web_server.cpp      # WiFi AP + web server + mobile API endpoints
│   └── api_handlers.cpp    # Mobile app API endpoint handlers
└── data/
    └── sd/
        └── db/             # Default empty JSON files for SD card
            ├── users.json          # []
            ├── announcements.json  # []
            ├── conversations.json  # []
            ├── direct_messages.json # []
            ├── fog_devices.json    # []
            └── messages.json       # []
```

---

## 6. Implementation: config.h

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ───────────────────────────────────────────────
// Each node creates its own WiFi network.
// Mobile phones connect to this to access the local API.
#define AP_SSID       "HopFog-Node-01"    // Change for each node!
#define AP_PASSWORD   "changeme123"
#define AP_CHANNEL    6                    // Use different channel from admin (1)
#define AP_MAX_CONN   4

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Node Identity ───────────────────────────────────────────────────
#define NODE_ID       "node-01"           // Unique ID for this node
#define DEVICE_NAME   "HopFog-Node-01"    // Human-readable name

// ── SD Card (ESP32-CAM built-in slot, 1-bit SD_MMC mode) ───────────
#define SD_DB_DIR           "/db"
#define SD_USERS_FILE       "/db/users.json"
#define SD_ANNOUNCE_FILE    "/db/announcements.json"
#define SD_CONVOS_FILE      "/db/conversations.json"
#define SD_DMS_FILE         "/db/direct_messages.json"
#define SD_FOG_FILE         "/db/fog_devices.json"
#define SD_MSGS_FILE        "/db/messages.json"

// ── XBee S2C (ZigBee) ──────────────────────────────────────────────
// Uses UART2 (Serial2) so UART0 (Serial) stays free for Serial Monitor.
#define XBEE_BAUD       9600
#define XBEE_TX_PIN     13    // ESP32 TX → XBee DIN  (pin 3)
#define XBEE_RX_PIN     12    // ESP32 RX ← XBee DOUT (pin 2)

// ── Timing ─────────────────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000   // Send REGISTER every 10s until ACK
#define HEARTBEAT_INTERVAL_MS  30000   // Send HEARTBEAT every 30s after registered
#define SYNC_RETRY_MS          15000   // Retry SYNC_REQUEST if no response

// ── JSON buffer ────────────────────────────────────────────────────
#ifdef BOARD_HAS_PSRAM
  #define JSON_DOC_SIZE  16384
#else
  #define JSON_DOC_SIZE   8192
#endif

#endif // CONFIG_H
```

---

## 7. Implementation: xbee_comm.h

This must be **byte-for-byte compatible** with the admin's XBee driver. The admin builds 0x10 Transmit Request frames and parses 0x90 Receive Packet frames. The node must do the same.

```cpp
#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── API Mode 1 Constants ────────────────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10   // Transmit Request frame type
#define XBEE_RX_PACKET     0x90   // Receive Packet frame type
#define XBEE_TX_STATUS     0x8B   // Transmit Status frame type
#define XBEE_MAX_FRAME     512

// ── Callback for received RF data ───────────────────────────────────
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

// ── Public API ──────────────────────────────────────────────────────
void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
void xbeeProcessIncoming();
void xbeeSetReceiveCallback(XBeeReceiveCB cb);

#endif // XBEE_COMM_H
```

---

## 8. Implementation: xbee_comm.cpp

**This is the most critical file.** It must produce identical API frames to the admin.

```cpp
#include "xbee_comm.h"
#include "config.h"
#include <driver/uart.h>
#include <driver/gpio.h>

static HardwareSerial& xbeeSerial = Serial2;
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;

// ── Frame receive state machine ─────────────────────────────────────
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };
static RxState   rxState = WAIT_DELIM;
static uint16_t  rxFrameLen = 0;
static uint16_t  rxIdx = 0;
static uint8_t   rxFrame[XBEE_MAX_FRAME];
static uint8_t   rxChecksum = 0;

void xbeeInit() {
    // *** CRITICAL: On ESP32-CAM, SD_MMC.begin() claims GPIO 12/13 via ***
    // *** IOMUX as HS2_DATA2/DATA3.  IOMUX takes priority over the     ***
    // *** GPIO matrix that UART2 uses.  gpio_reset_pin() detaches the   ***
    // *** pins from IOMUX so UART2 can claim them.                      ***
#ifdef USE_SD_MMC
    gpio_reset_pin(GPIO_NUM_12);
    gpio_reset_pin(GPIO_NUM_13);
    Serial.println("[XBee] Reset GPIO 12/13 from SD_MMC IOMUX");
#endif

    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);

    // Explicitly route UART2 signals to these pins (belt-and-suspenders)
    uart_set_pin(UART_NUM_2, XBEE_TX_PIN, XBEE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.printf("[XBee] UART2 init: TX=GPIO%d RX=GPIO%d baud=%d (API mode 1)\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;  // 14 bytes header + payload
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    // Build the 14-byte header for a 0x10 Transmit Request
    uint8_t hdr[14] = {
        XBEE_TX_REQUEST,          // Frame type: 0x10
        fid,                      // Frame ID (1-255)
        0x00, 0x00, 0x00, 0x00,   // 64-bit dest addr (broadcast)
        0x00, 0x00, 0xFF, 0xFF,   //   = 0x000000000000FFFF
        0xFF, 0xFE,               // 16-bit dest addr (broadcast)
        0x00,                     // Broadcast radius (0 = max)
        0x00                      // Options (0 = none)
    };

    // Calculate checksum: sum all frame data bytes (hdr + payload), then 0xFF - sum
    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Write the complete frame: [0x7E] [LenHi] [LenLo] [header] [payload] [checksum]
    xbeeSerial.write(XBEE_START_DELIM);              // 0x7E
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));   // Length high byte
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF)); // Length low byte
    xbeeSerial.write(hdr, 14);                        // Header (14 bytes)
    xbeeSerial.write((const uint8_t*)payload, len);   // Payload (JSON)
    xbeeSerial.write(cksum);                          // Checksum
    xbeeSerial.flush();                               // Wait for TX complete

    Serial.printf("[XBee] TX frame ID=%d (%d bytes)\n", fid, (int)len);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) {
    rxCallback = cb;
}

void xbeeProcessIncoming() {
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
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) {
                Serial.printf("[XBee] Invalid frame length %d\n", rxFrameLen);
                rxState = WAIT_DELIM;
            } else {
                rxState = READING_DATA;
            }
            break;

        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = GOT_CHECKSUM;
            break;

        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                uint8_t frameType = rxFrame[0];

                if (frameType == XBEE_RX_PACKET && rxFrameLen >= 13) {
                    // 0x90 Receive Packet: extract RF data from byte 12 onward
                    const char* rfData = (const char*)&rxFrame[12];
                    size_t rfLen = rxFrameLen - 12;

                    // Strip trailing newlines
                    while (rfLen > 0 && (rfData[rfLen-1] == '\n' || rfData[rfLen-1] == '\r'))
                        rfLen--;

                    if (rfLen > 0 && rfLen < XBEE_MAX_FRAME - 12) {
                        rxFrame[12 + rfLen] = '\0';  // null-terminate
                        Serial.printf("[XBee] RX 0x90 (%d bytes): %.80s\n", (int)rfLen, rfData);
                        if (rxCallback) rxCallback(rfData, rfLen);
                    }
                } else if (frameType == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    // 0x8B Transmit Status
                    uint8_t delivery = rxFrame[5];
                    if (delivery == 0) {
                        Serial.printf("[XBee] TX status: OK (frame %d)\n", rxFrame[1]);
                    } else {
                        Serial.printf("[XBee] TX status: FAILED 0x%02X (frame %d)\n",
                                      delivery, rxFrame[1]);
                    }
                } else if (frameType == XBEE_TX_REQUEST) {
                    // Self-echo — our own TX frame looping back. Ignore.
                    Serial.println("[XBee] Self-echo (0x10) — ignored");
                } else {
                    Serial.printf("[XBee] Unknown frame type 0x%02X\n", frameType);
                }
            } else {
                Serial.printf("[XBee] Checksum error (0x%02X != 0xFF)\n", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
```

---

## 9. Implementation: node_client.h

The node protocol client handles the state machine for registration, heartbeat, and sync.

```cpp
#ifndef NODE_CLIENT_H
#define NODE_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Node state ─────────────────────────────────────────────────────
enum NodeState {
    STATE_UNREGISTERED,   // Sending REGISTER every 10s
    STATE_REGISTERED,     // Got REGISTER_ACK, now syncing
    STATE_SYNCING,        // Sent SYNC_REQUEST, waiting for SYNC_DATA
    STATE_RUNNING         // Fully synced, sending HEARTBEAT every 30s
};

/// Initialize the node client (call once in setup, after xbeeInit).
void nodeClientInit();

/// Call from loop() — handles timers for REGISTER/HEARTBEAT/SYNC.
void nodeClientLoop();

/// Handle an incoming JSON command from admin (call from XBee RX callback).
/// Returns true if it was a recognized admin command.
bool nodeClientHandleCommand(const char* payload, size_t len);

/// Get current state (for diagnostic display).
NodeState nodeClientGetState();

#endif // NODE_CLIENT_H
```

---

## 10. Implementation: node_client.cpp

```cpp
#include "node_client.h"
#include "config.h"
#include "xbee_comm.h"
#include "sd_storage.h"

static NodeState state = STATE_UNREGISTERED;
static unsigned long lastRegisterMs  = 0;
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastSyncMs      = 0;

// ── Helper: send a JSON command via XBee ────────────────────────────
static void sendCommand(JsonDocument& doc) {
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// ── Outgoing commands ───────────────────────────────────────────────

static void sendRegister() {
    JsonDocument doc;
    doc["cmd"] = "REGISTER";
    JsonObject params = doc["params"].to<JsonObject>();
    params["device_name"] = DEVICE_NAME;
    params["ip_address"] = WiFi.softAPIP().toString();
    params["status"] = "active";
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
    Serial.println("[Node] Sent REGISTER");
}

static void sendHeartbeat() {
    JsonDocument doc;
    doc["cmd"] = "HEARTBEAT";
    JsonObject params = doc["params"].to<JsonObject>();
    params["ip_address"] = WiFi.softAPIP().toString();
    params["uptime"] = (int)(millis() / 1000);
    params["free_heap"] = (int)ESP.getFreeHeap();
    sendCommand(doc);
}

static void sendSyncRequest() {
    JsonDocument doc;
    doc["cmd"] = "SYNC_REQUEST";
    sendCommand(doc);
    Serial.println("[Node] Sent SYNC_REQUEST");
}

// ── Incoming command handlers ───────────────────────────────────────

static void handleRegisterAck() {
    Serial.println("[Node] Got REGISTER_ACK — registered with admin!");
    state = STATE_REGISTERED;
    // Immediately request data sync
    sendSyncRequest();
    lastSyncMs = millis();
    state = STATE_SYNCING;
}

static void handlePong() {
    Serial.println("[Node] Got PONG");
}

static void handleSyncData(JsonDocument& doc) {
    Serial.println("[Node] Got SYNC_DATA — saving to SD card...");

    // Save each data category to its own file
    if (doc["users"].is<JsonArray>()) {
        JsonDocument usersDoc;
        usersDoc.set(doc["users"]);
        writeJsonFile(SD_USERS_FILE, usersDoc);
        Serial.printf("[Node] Saved %d users\n", doc["users"].as<JsonArray>().size());
    }

    if (doc["announcements"].is<JsonArray>()) {
        JsonDocument annDoc;
        annDoc.set(doc["announcements"]);
        writeJsonFile(SD_ANNOUNCE_FILE, annDoc);
        Serial.printf("[Node] Saved %d announcements\n", doc["announcements"].as<JsonArray>().size());
    }

    if (doc["conversations"].is<JsonArray>()) {
        JsonDocument convDoc;
        convDoc.set(doc["conversations"]);
        writeJsonFile(SD_CONVOS_FILE, convDoc);
    }

    if (doc["chat_messages"].is<JsonArray>()) {
        JsonDocument dmDoc;
        dmDoc.set(doc["chat_messages"]);
        writeJsonFile(SD_DMS_FILE, dmDoc);
    }

    if (doc["fog_nodes"].is<JsonArray>()) {
        JsonDocument fogDoc;
        fogDoc.set(doc["fog_nodes"]);
        writeJsonFile(SD_FOG_FILE, fogDoc);
    }

    state = STATE_RUNNING;
    lastHeartbeatMs = millis();
    Serial.println("[Node] Sync complete — now in RUNNING state");
}

static void handleBroadcastMsg(JsonObject params) {
    // Admin sent a broadcast announcement — store it locally
    Serial.printf("[Node] Broadcast: %s\n", (const char*)(params["message"] | ""));

    JsonDocument doc;
    readJsonFile(SD_ANNOUNCE_FILE, doc);
    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    int newId = 1;
    for (JsonObject a : arr) {
        int id = a["id"] | 0;
        if (id >= newId) newId = id + 1;
    }

    JsonObject ann = arr.add<JsonObject>();
    ann["id"] = newId;
    ann["title"] = params["subject"] | params["message"] | "";
    ann["message"] = params["message"] | "";
    ann["created_at"] = String((long)(millis() / 1000));

    writeJsonFile(SD_ANNOUNCE_FILE, doc);
}

static void handleGetStats() {
    Serial.println("[Node] Admin requested stats");
    JsonDocument doc;
    doc["cmd"] = "STATS_RESPONSE";
    JsonObject params = doc["params"].to<JsonObject>();
    params["free_heap"] = (int)ESP.getFreeHeap();
    params["uptime"] = (int)(millis() / 1000);
    params["ip_address"] = WiFi.softAPIP().toString();
    params["wifi_stations"] = WiFi.softAPgetStationNum();
    sendCommand(doc);
}

// ── Public API ──────────────────────────────────────────────────────

void nodeClientInit() {
    state = STATE_UNREGISTERED;
    lastRegisterMs = 0;
    lastHeartbeatMs = 0;
    lastSyncMs = 0;
    Serial.println("[Node] Client initialized — will start REGISTER cycle");
}

void nodeClientLoop() {
    unsigned long now = millis();

    switch (state) {
    case STATE_UNREGISTERED:
        // Send REGISTER every 10 seconds until we get ACK
        if (now - lastRegisterMs >= REGISTER_INTERVAL_MS) {
            sendRegister();
            lastRegisterMs = now;
        }
        break;

    case STATE_REGISTERED:
        // Just registered — waiting to start sync (handled in handleRegisterAck)
        break;

    case STATE_SYNCING:
        // Waiting for SYNC_DATA — retry if no response
        if (now - lastSyncMs >= SYNC_RETRY_MS) {
            sendSyncRequest();
            lastSyncMs = now;
        }
        break;

    case STATE_RUNNING:
        // Send HEARTBEAT every 30 seconds
        if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
            sendHeartbeat();
            lastHeartbeatMs = now;
        }
        break;
    }
}

bool nodeClientHandleCommand(const char* payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return false;

    const char* cmd = doc["cmd"];
    if (!cmd) return false;

    Serial.printf("[Node] RX cmd: %s\n", cmd);

    if (strcmp(cmd, "REGISTER_ACK") == 0) {
        handleRegisterAck();
    } else if (strcmp(cmd, "PONG") == 0) {
        handlePong();
    } else if (strcmp(cmd, "SYNC_DATA") == 0) {
        handleSyncData(doc);
    } else if (strcmp(cmd, "BROADCAST_MSG") == 0) {
        handleBroadcastMsg(doc["params"].as<JsonObject>());
    } else if (strcmp(cmd, "GET_STATS") == 0) {
        handleGetStats();
    } else {
        Serial.printf("[Node] Unknown admin command: %s\n", cmd);
        return false;
    }
    return true;
}

NodeState nodeClientGetState() {
    return state;
}
```

---

## 11. Implementation: sd_storage.h / .cpp

### sd_storage.h

```cpp
#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

/// Initialize the SD card. Returns true on success.
bool initSDCard();

/// Read a JSON file from SD into a JsonDocument.
/// Returns true if file exists and was parsed.
bool readJsonFile(const char* path, JsonDocument& doc);

/// Write a JsonDocument to a file on SD (overwrites).
bool writeJsonFile(const char* path, JsonDocument& doc);

#endif // SD_STORAGE_H
```

### sd_storage.cpp

```cpp
#include "sd_storage.h"
#include "config.h"
#include <SD_MMC.h>

bool initSDCard() {
    // ESP32-CAM flash LED (GPIO 4) can interfere with SD bus — keep it OFF
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("[SD] SD_MMC init FAILED");
        return false;
    }

    Serial.printf("[SD] Card mounted — size: %lluMB\n",
                  SD_MMC.totalBytes() / (1024 * 1024));

    // Create /db directory if it doesn't exist
    if (!SD_MMC.exists(SD_DB_DIR)) {
        SD_MMC.mkdir(SD_DB_DIR);
    }

    // Create default empty JSON files if they don't exist
    const char* files[] = {
        SD_USERS_FILE, SD_ANNOUNCE_FILE, SD_CONVOS_FILE,
        SD_DMS_FILE, SD_FOG_FILE, SD_MSGS_FILE
    };
    for (const char* f : files) {
        if (!SD_MMC.exists(f)) {
            File file = SD_MMC.open(f, FILE_WRITE);
            if (file) {
                file.print("[]");
                file.close();
            }
        }
    }

    return true;
}

bool readJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[SD] File not found: %s\n", path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[SD] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

bool writeJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[SD] Cannot write: %s\n", path);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
```

---

## 12. Implementation: Web Server & API Handlers

The node should run its own WiFi AP and serve the same mobile app API endpoints as the admin. Mobile phones connect to the node's WiFi and use these endpoints.

### Minimum Required Endpoints

These are the endpoints the HopFogMobile app calls:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /status` | GET | Health check — `{"online": true}` |
| `POST /login` | POST | Login — JSON `{username, password}` → user object |
| `GET /announcements` | GET | List announcements (from synced data) |
| `GET /users` | GET | List users (from synced data) |
| `GET /conversations` | GET | List conversations for a user |
| `GET /messages` | GET | List messages in a conversation |
| `POST /send` | POST | Send a message (store locally + relay to admin) |
| `POST /create-chat` | POST | Create a conversation (store locally + relay) |
| `POST /sos` | POST | Trigger SOS alert (relay to admin immediately) |
| `GET /new-messages` | GET | Poll for new messages |
| `POST /agree-sos` | POST | Record SOS agreement |
| `POST /change-password` | POST | Change password (relay to admin) |

### How to Relay User Actions to Admin

When a mobile user performs an action (send message, SOS, etc.) on the node:
1. Store the data locally on the node's SD card
2. Send a relay command to admin via XBee:

```cpp
// Example: relay a chat message to admin
void relayChatMessage(int convId, int senderId, const char* text) {
    JsonDocument doc;
    doc["cmd"] = "RELAY_CHAT_MSG";
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    JsonObject params = doc["params"].to<JsonObject>();
    params["conversation_id"] = convId;
    params["sender_id"] = senderId;
    params["message_text"] = text;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// Example: relay SOS alert to admin
void relaySosAlert(int userId) {
    JsonDocument doc;
    doc["cmd"] = "SOS_ALERT";
    doc["node_id"] = NODE_ID;
    doc["ts"] = (long)(millis() / 1000);
    JsonObject params = doc["params"].to<JsonObject>();
    params["user_id"] = userId;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}

// Example: relay password change to admin
void relayChangePassword(int userId, const char* oldPw, const char* newPw) {
    JsonDocument doc;
    doc["cmd"] = "CHANGE_PASSWORD";
    doc["node_id"] = NODE_ID;
    JsonObject params = doc["params"].to<JsonObject>();
    params["user_id"] = userId;
    params["old_password"] = oldPw;
    params["new_password"] = newPw;
    String json;
    serializeJson(doc, json);
    xbeeSendBroadcast(json.c_str(), json.length());
}
```

---

## 13. Implementation: main.cpp

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "sd_storage.h"
#include "xbee_comm.h"
#include "node_client.h"

// If you add ESPAsyncWebServer for mobile API:
// #include <ESPAsyncWebServer.h>
// AsyncWebServer server(HTTP_PORT);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("   HopFog-Node  ESP32 Firmware");
    Serial.println("========================================");

    // 1. SD card
    if (!initSDCard()) {
        Serial.println("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 2. WiFi access point
    Serial.printf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power-saving for stability
    Serial.printf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 3. Web server with mobile API endpoints
    // setupWebServer(server);
    // registerApiHandlers(server);
    // server.begin();

    // 4. XBee + node protocol
    xbeeInit();
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            Serial.printf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    Serial.println("[Node] Setup complete — starting REGISTER cycle");
}

void loop() {
    xbeeProcessIncoming();
    nodeClientLoop();
    yield();
}
```

---

## 14. Protocol Reference

### Frame Structure (API Mode 1)

Every XBee serial exchange uses binary API frames:

```
┌──────┬──────────────┬───────────────────────────────────┬──────────┐
│ 0x7E │ Length (2B)   │ Frame Data (variable)             │ Checksum │
│ 1B   │ Hi Lo        │ [Type][...header...][payload]      │ 1B       │
└──────┴──────────────┴───────────────────────────────────┴──────────┘
```

**Checksum:** `0xFF - (sum of all frame data bytes)`

### 0x10 Transmit Request (Node → XBee → Air → Admin's XBee → 0x90)

```
Frame data (14 + N bytes):
  [0]     = 0x10  (frame type)
  [1]     = Frame ID (1-255)
  [2-9]   = 64-bit destination: 00 00 00 00 00 00 FF FF (broadcast)
  [10-11] = 16-bit destination: FF FE (broadcast)
  [12]    = Broadcast radius: 00 (max hops)
  [13]    = Options: 00
  [14..N] = RF data payload (your JSON string)
```

### 0x90 Receive Packet (Remote XBee → Air → Local XBee → ESP32)

```
Frame data (12 + N bytes):
  [0]     = 0x90  (frame type)
  [1-8]   = 64-bit source address
  [9-10]  = 16-bit source address
  [11]    = Receive options
  [12..N] = RF data payload (the JSON string)
```

### 0x8B Transmit Status (XBee → ESP32, confirms TX delivery)

```
Frame data (7 bytes):
  [0]     = 0x8B  (frame type)
  [1]     = Frame ID (matches your TX request)
  [2-3]   = 16-bit destination
  [4]     = Retry count
  [5]     = Delivery status: 0x00 = success, others = failure
  [6]     = Discovery status
```

### JSON Command Format

All JSON payloads follow this structure:
```json
{"cmd":"COMMAND_NAME","node_id":"node-01","ts":12345,"params":{...}}
```

### Node → Admin Commands

| Command | When | Params |
|---------|------|--------|
| `REGISTER` | Every 10s until ACK | `{device_name, ip_address, status, free_heap}` |
| `HEARTBEAT` | Every 30s after registered | `{ip_address, uptime, free_heap}` |
| `SYNC_REQUEST` | After REGISTER_ACK | _(none)_ |
| `RELAY_CHAT_MSG` | When mobile user sends a message | `{conversation_id, sender_id, message_text}` |
| `SOS_ALERT` | When mobile user triggers SOS | `{user_id}` |
| `CHANGE_PASSWORD` | When mobile user changes password | `{user_id, old_password, new_password}` |
| `RELAY_FOG_NODE` | When a fog device connects to node | `{device_name, ip_address, status}` |
| `STATS_RESPONSE` | In response to GET_STATS | `{free_heap, uptime, ip_address, wifi_stations}` |

### Admin → Node Commands

| Command | When |
|---------|------|
| `REGISTER_ACK` | In response to REGISTER |
| `PONG` | In response to HEARTBEAT |
| `SYNC_DATA` | In response to SYNC_REQUEST — contains `{users, announcements, conversations, chat_messages, fog_nodes}` |
| `BROADCAST_MSG` | When admin creates a broadcast |
| `GET_STATS` | When admin wants node stats |

### Command Flow Diagram

```
Node                                     Admin
  │                                        │
  ├──► REGISTER ──────────────────────────►│
  │    (every 10s)                         │
  │                                        │
  │◄──────────────────── REGISTER_ACK ◄────┤
  │                                        │
  ├──► SYNC_REQUEST ──────────────────────►│
  │                                        │
  │◄──────────────────── SYNC_DATA ◄───────┤
  │    (users, announcements, etc.)        │
  │                                        │
  ├──► HEARTBEAT ─────────────────────────►│
  │    (every 30s)                         │
  │◄──────────────────── PONG ◄────────────┤
  │                                        │
  │    [Mobile user sends message]         │
  ├──► RELAY_CHAT_MSG ────────────────────►│
  │                                        │
  │    [Admin creates broadcast]           │
  │◄──────────────────── BROADCAST_MSG ◄───┤
  │                                        │
```

---

## 15. CRITICAL: GPIO Fix for ESP32-CAM

**This is the #1 cause of "no communication" on ESP32-CAM boards.**

On ESP32-CAM, the SD card uses `SD_MMC.begin()` which configures GPIO 12 and 13 via the **IOMUX** peripheral as HS2_DATA2 and HS2_DATA3 (even in 1-bit mode where they're not needed). The UART2 peripheral routes through the **GPIO matrix**, which has lower priority than IOMUX.

**Result:** UART TX still works (output can override), but UART RX does NOT work (input is routed to the SD peripheral instead of UART2).

**Fix:** Call `gpio_reset_pin()` BEFORE `Serial2.begin()`:

```cpp
#include <driver/gpio.h>
#include <driver/uart.h>

// This MUST be called AFTER SD_MMC.begin() and BEFORE Serial2.begin()
gpio_reset_pin(GPIO_NUM_12);   // Detach from HS2_DATA2
gpio_reset_pin(GPIO_NUM_13);   // Detach from HS2_DATA3

Serial2.begin(9600, SERIAL_8N1, 12, 13);

uart_set_pin(UART_NUM_2, 13, 12,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

**The admin has confirmed this fix works.** Without it, the loopback test fails (write 4 bytes, read 0 back). With it, RX works correctly.

---

## 16. Debugging Guide

### Step 1: Verify XBee Hardware
1. Power on both admin and node
2. Check XBee ASSOC LED: blinking = searching, steady = joined
3. In XCTU: read `AI` parameter on node XBee → must be `0x00`

### Step 2: Verify ESP32 Serial
1. Open Serial Monitor on the node (115200 baud)
2. Look for: `[XBee] UART2 init: TX=GPIO13 RX=GPIO12 baud=9600 (API mode 1)`
3. Look for: `[Node] Sent REGISTER` (should appear every 10 seconds)
4. If no REGISTER prints, the node's main loop or timer isn't running

### Step 3: Verify TX Works
1. On admin: open Testing page → Serial Monitor section
2. On node: wait for REGISTER to be sent
3. Admin should show: `RX <- [0x90] from XXXX (...bytes) {"cmd":"REGISTER",...}`
4. If admin shows nothing → node's XBee is not transmitting, or XBees aren't associated

### Step 4: Verify RX Works
1. On admin: click "Send Test Message"
2. Node serial monitor should show: `[XBee] RX 0x90 (...bytes): {"cmd":"BROADCAST_MSG",...}`
3. If node shows nothing → check `gpio_reset_pin()` fix, check wiring

### Step 5: Monitor Full Handshake
```
Node serial output:
  [Node] Sent REGISTER
  [XBee] TX status: OK (frame 1)        ← XBee accepted our frame
  [XBee] RX 0x90 (28 bytes): {"cmd":"REGISTER_ACK","node_id":"node-01"}
  [Node] Got REGISTER_ACK — registered!
  [Node] Sent SYNC_REQUEST
  [XBee] RX 0x90 (1234 bytes): {"cmd":"SYNC_DATA","users":[...],...}
  [Node] Sync complete — now in RUNNING state
```

---

## 17. Implementation Checklist

### Phase 1 — XBee Communication (Get REGISTER working)
- [ ] Create `platformio.ini` with ESP32-CAM + libraries
- [ ] Create `config.h` with pin definitions and node identity
- [ ] Create `xbee_comm.h` and `xbee_comm.cpp` (API mode 1 driver)
  - [ ] **Include `gpio_reset_pin()` fix** (most critical!)
  - [ ] `xbeeInit()` — UART2 setup with gpio fix
  - [ ] `xbeeSendBroadcast()` — build 0x10 TX Request frame
  - [ ] `xbeeProcessIncoming()` — state machine to parse 0x90 RX frames
- [ ] Create `sd_storage.h` and `sd_storage.cpp`
- [ ] Create `node_client.h` and `node_client.cpp`
  - [ ] `sendRegister()` — send REGISTER every 10s
  - [ ] `handleRegisterAck()` — transition to registered state
- [ ] Create minimal `main.cpp` (SD init + WiFi AP + XBee + node client)
- [ ] **Test:** Flash, open serial monitor, verify "Sent REGISTER" appears
- [ ] **Test:** Check admin serial monitor — should show 0x90 frame with REGISTER

### Phase 2 — Data Sync
- [ ] Implement `handleSyncData()` — save SYNC_DATA to SD card files
- [ ] Implement `sendSyncRequest()` — request full data after REGISTER_ACK
- [ ] Implement `sendHeartbeat()` — 30-second heartbeat cycle
- [ ] Implement `handleBroadcastMsg()` — save admin broadcasts to SD
- [ ] Implement `handleGetStats()` — respond with node stats
- [ ] **Test:** Admin serial monitor shows SYNC_REQUEST, node receives SYNC_DATA

### Phase 3 — Mobile App API
- [ ] Add ESPAsyncWebServer with mobile API endpoints
- [ ] `GET /status` → `{"online": true}`
- [ ] `POST /login` → authenticate from synced users.json
- [ ] `GET /announcements` → return from synced announcements.json
- [ ] `GET /users`, `GET /conversations`, `GET /messages`, `GET /new-messages`
- [ ] `POST /send` → store locally + relay via `RELAY_CHAT_MSG`
- [ ] `POST /sos` → relay via `SOS_ALERT`
- [ ] `POST /create-chat` → store locally
- [ ] `POST /change-password` → relay via `CHANGE_PASSWORD`
- [ ] `POST /agree-sos` → update synced users.json locally
- [ ] **Test:** Connect phone to node WiFi, open HopFogMobile app

---

## Appendix: Admin Diagnostic Endpoints

If you need to debug from the admin side, these endpoints are available on the admin web UI:

| Endpoint | Method | What it does |
|----------|--------|-------------|
| `GET /api/xbee/rx-log` | GET | Returns last 50 serial events (TX, RX, errors) |
| `GET /api/xbee/diagnostics` | GET | Full diagnostic with GPIO states, hex dump, assessment |
| `POST /api/xbee/test` | POST | Send a test broadcast message |
| `POST /api/xbee/send-raw` | POST | Send arbitrary text via XBee |
| `GET /api/nodes` | GET | List registered nodes |
| `POST /api/nodes/{id}/sync` | POST | Manually trigger SYNC_DATA to a node |
| `POST /api/nodes/{id}/get-stats` | POST | Request stats from a node |
