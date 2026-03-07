# HopFog-Node — Incremental Change Log

> **Purpose:** This file documents changes to give to the Copilot agent
> working on [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node).
> Each change is self-contained and under 30K characters.
> Copy the relevant change section and paste it as the task prompt.

---

## Change #1: Switch from SD_MMC to SPI SD (Critical — XBee RX fix)

**Date:** 2026-03-07
**Priority:** CRITICAL — without this, XBee RX does not work
**Branch:** `copilot/setup-and-xbee-driver`

### Problem

The node currently uses `SD_MMC.begin("/sdcard", true)` for the ESP32-CAM
built-in SD card slot. Even in 1-bit mode, the SD_MMC peripheral claims
GPIO 12 and 13 via **IOMUX** (as HS2_DATA2 and HS2_DATA3). IOMUX has
hardware priority over the GPIO matrix that UART2 uses for XBee
communication. Result: XBee TX works (output can override) but **XBee RX
fails** because GPIO 12 input is routed to the SD peripheral instead of
UART2.

The `gpio_reset_pin()` workaround currently in the node's xbee_comm.cpp
does NOT fix this because the SDMMC host driver re-asserts IOMUX on every
SD card transaction.

### Solution

Switch the ESP32-CAM SD card access from SD_MMC to **SPI mode** using the
HSPI bus. The built-in SD slot supports both protocols. In SPI mode, only
4 pins are used (CS=13, CLK=14, MISO=2, MOSI=15), freeing GPIO 4 and 12
completely. XBee then uses GPIO 4 (TX) and GPIO 12 (RX) with no conflicts.

### What to change

**File 1: `platformio.ini`**

Replace the entire file with:

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
    -DASYNCWEBSERVER_REGEX
```

Changes: `-DUSE_SD_MMC=1` → `-DESP32CAM_SPI_SD=1`, added `-DASYNCWEBSERVER_REGEX`

---

**File 2: `include/config.h`**

Replace the SD Card and XBee sections. The full file should be:

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ───────────────────────────────────────────────
#define AP_SSID       "HopFog-Node-01"
#define AP_PASSWORD   "changeme123"
#define AP_CHANNEL    6
#define AP_MAX_CONN   4

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Node Identity ───────────────────────────────────────────────────
#define NODE_ID       "node-01"
#define DEVICE_NAME   "HopFog-Node-01"

// ── SD Card (ESP32-CAM built-in slot — SPI mode) ────────────────────
//
// Uses SPI (NOT SD_MMC) to avoid GPIO 12/13 IOMUX conflict with UART2.
// The SD_MMC peripheral permanently claims GPIO 12/13 even in 1-bit mode,
// preventing XBee serial communication on those pins.
//
// ESP32-CAM SD slot hardware wiring:
//   CS   = GPIO 13 (was DAT3 in SDMMC mode)
//   CLK  = GPIO 14
//   MISO = GPIO 2  (was DAT0)
//   MOSI = GPIO 15 (was CMD)
//
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
//
// ESP32-CAM pin assignment (SPI SD mode frees GPIO 4 and 12):
//   GPIO 4  = XBee TX (→ DIN)  — also has flash LED, will flicker during TX
//   GPIO 12 = XBee RX (← DOUT) — was SD_MMC DAT2, now free in SPI mode
//
// Note: GPIO 12 is a boot-strapping pin. If the ESP32 fails to boot
//       with XBee connected, disconnect XBee DOUT during power-on.
#define XBEE_BAUD       9600
#define XBEE_TX_PIN     4     // ESP32 TX → XBee DIN  (was 13, conflicts with SD CS)
#define XBEE_RX_PIN     12    // ESP32 RX ← XBee DOUT (free in SPI SD mode)

// ── Timing ─────────────────────────────────────────────────────────
#define REGISTER_INTERVAL_MS   10000
#define HEARTBEAT_INTERVAL_MS  30000
#define SYNC_RETRY_MS          15000

// ── JSON buffer ────────────────────────────────────────────────────
#ifdef BOARD_HAS_PSRAM
  #define JSON_DOC_SIZE  16384
#else
  #define JSON_DOC_SIZE   8192
#endif

#endif // CONFIG_H
```

Key changes:
- Added `SD_CS_PIN`, `SD_SPI_CLK`, `SD_SPI_MISO`, `SD_SPI_MOSI` defines
- Changed `XBEE_TX_PIN` from 13 → **4**
- `XBEE_RX_PIN` stays 12 (but now it actually works because SD_MMC isn't claiming it)

---

**File 3: `src/sd_storage.cpp`**

Replace the entire file with:

```cpp
#include "sd_storage.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

#define SD_FS SD

bool initSDCard() {
    Serial.println("[SD] Initialising SD card...");

#ifdef ESP32CAM_SPI_SD
    // ESP32-CAM: use SPI mode to access the built-in SD card slot.
    // This avoids the SD_MMC peripheral which permanently claims
    // GPIO 12/13 via IOMUX, preventing UART2 (XBee) from using them.
    // Static so the SPI bus object persists (SD library holds a reference).
    static SPIClass spiSD(HSPI);
    spiSD.begin(SD_SPI_CLK, SD_SPI_MISO, SD_SPI_MOSI, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, spiSD)) {
        Serial.println("[SD] SPI SD mount failed!");
        return false;
    }
    Serial.println("[SD] SPI mode (HSPI) — mounted OK");
#else
    if (!SD.begin()) {
        Serial.println("[SD] Mount failed!");
        return false;
    }
#endif

    Serial.printf("[SD] Card size: %lluMB\n",
                  SD_FS.totalBytes() / (1024 * 1024));

    // Create /db directory if needed
    if (!SD_FS.exists(SD_DB_DIR)) {
        SD_FS.mkdir(SD_DB_DIR);
    }

    // Seed empty JSON array files
    const char* files[] = {
        SD_USERS_FILE, SD_ANNOUNCE_FILE, SD_CONVOS_FILE,
        SD_DMS_FILE, SD_FOG_FILE, SD_MSGS_FILE
    };
    for (const char* f : files) {
        if (!SD_FS.exists(f)) {
            File file = SD_FS.open(f, FILE_WRITE);
            if (file) {
                file.print("[]");
                file.close();
            }
        }
    }

    return true;
}

bool readJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_FS.open(path, FILE_READ);
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
    File file = SD_FS.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[SD] Cannot write: %s\n", path);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
```

Key changes:
- `#include <SD_MMC.h>` → `#include <SD.h>` + `#include <SPI.h>`
- `SD_MMC.begin("/sdcard", true)` → `SD.begin(SD_CS_PIN, spiSD)` with HSPI
- All `SD_MMC.xxx()` calls → `SD_FS.xxx()` (where `SD_FS` = `SD`)
- Removed `pinMode(4, OUTPUT); digitalWrite(4, LOW);` (GPIO 4 is now XBee TX)

---

**File 4: `include/sd_storage.h`**

Make sure it does NOT include SD_MMC.h. It should be:

```cpp
#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <ArduinoJson.h>

bool initSDCard();
bool readJsonFile(const char* path, JsonDocument& doc);
bool writeJsonFile(const char* path, JsonDocument& doc);

#endif
```

---

**File 5: `src/xbee_comm.cpp`**

Remove the `gpio_reset_pin()` hack — it's no longer needed since we're
not using SD_MMC. Remove `#include <driver/gpio.h>`.

Replace the xbeeInit() function:

```cpp
void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);

    // Explicitly route UART2 signals to our chosen GPIO pins
    uart_set_pin(UART_NUM_2, XBEE_TX_PIN, XBEE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.printf("[XBee] UART2 init: TX=GPIO%d RX=GPIO%d baud=%d (API mode 1)\n",
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}
```

The full top of `xbee_comm.cpp` should start with:

```cpp
#include "xbee_comm.h"
#include "config.h"
#include <driver/uart.h>

static HardwareSerial& xbeeSerial = Serial2;
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;
```

(No `#include <driver/gpio.h>`, no `#ifdef USE_SD_MMC` block.)

---

### Wiring (new — both admin and node)

```
ESP32-CAM Pin    →    XBee S2C Pin
─────────────         ────────────
GPIO 4  (TX)    →    Pin 3 (DIN)
GPIO 12 (RX)    ←    Pin 2 (DOUT)
3.3V            →    Pin 1 (VCC)
GND             →    Pin 10 (GND)
```

**Important:** GPIO 4 is also the flash LED on ESP32-CAM. It will flicker
during XBee TX. This is normal and harmless.

### Verification

After flashing, the Serial Monitor should show:
```
[SD] SPI mode (HSPI) — mounted OK
[XBee] UART2 init: TX=GPIO4 RX=GPIO12 baud=9600 (API mode 1)
```

If it shows `SD_MMC` or `TX=GPIO13`, the old code is still being used.

### Checklist

- [ ] `platformio.ini`: `-DUSE_SD_MMC=1` → `-DESP32CAM_SPI_SD=1`
- [ ] `config.h`: Add SPI SD pin defines, change XBEE_TX_PIN from 13 → 4
- [ ] `sd_storage.cpp`: Replace SD_MMC with SPI SD (HSPI)
- [ ] `sd_storage.h`: Remove any SD_MMC include
- [ ] `xbee_comm.cpp`: Remove gpio_reset_pin hack and driver/gpio.h
- [ ] Rewire: XBee DIN from GPIO 13 → GPIO 4
- [ ] Build and verify Serial Monitor output

---

*End of Change #1*
