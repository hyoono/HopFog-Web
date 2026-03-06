# HopFog-Node — Build From Scratch (Part 1 of 3: Setup & XBee Driver)

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
15. [CRITICAL: SD Card SPI Mode for ESP32-CAM](#15-critical-sd-card-spi-mode-for-esp32-cam)
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
GPIO 4  (TX) ────► DIN  (pin 3)
GPIO 12 (RX) ◄──── DOUT (pin 2)
3.3V         ────► VCC  (pin 1)
GND          ────► GND  (pin 10)
```

**⚠️ GPIO 12 boot-strapping note:** GPIO 12 controls the flash voltage at boot. If the ESP32 fails to boot with XBee connected, either:
- Disconnect XBee DOUT from GPIO 12 during power-on, reconnect after boot
- Or burn the VDD_SDIO efuse to force 3.3V (permanent, one-time): `espefuse.py set_flash_voltage 3.3V`

### SD Card
The ESP32-CAM's built-in SD card slot is used via SPI (HSPI bus). The SPI pins are: CLK=GPIO 14, MISO=GPIO 2, MOSI=GPIO 15, CS=GPIO 13. This avoids the GPIO conflict that SD_MMC causes with UART2/XBee pins. Just insert a FAT32-formatted micro SD card.

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
    -DESP32CAM_SPI_SD=1
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

// ── SD Card (ESP32-CAM built-in slot, SPI mode via HSPI) ───────────
#ifdef ESP32CAM_SPI_SD
  #define SD_CS_PIN       13
  #define SD_SPI_CLK      14
  #define SD_SPI_MISO      2
  #define SD_SPI_MOSI     15
#endif
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
#ifdef ESP32CAM_SPI_SD
  #define XBEE_TX_PIN    4    // ESP32 TX → XBee DIN  (pin 3)
#else
  #define XBEE_TX_PIN   13    // Non-CAM boards: GPIO 13
#endif
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

