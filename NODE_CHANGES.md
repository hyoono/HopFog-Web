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

---

## Change #2: Fix Flash LED + Switch to UART1 + Move XBee TX (Critical)

**Date:** 2026-03-07
**Priority:** CRITICAL — fixes flash LED always on, and switches to UART1 to avoid PSRAM conflict
**Branch:** `copilot/setup-and-xbee-driver`
**Depends on:** Change #1 (SPI SD must be applied first)

### Problems

1. **Flash LED always ON:** GPIO 4 is the ESP32-CAM flash LED pin. Using it as UART TX causes the bright white LED to be permanently ON (UART idle = HIGH = LED on). Cannot be disabled in software while UART owns the pin.

2. **XBee RX still not working:** UART2's default pins (GPIO 16/17) are used by PSRAM on ESP32-CAM. Even though we remap to custom pins, the UART2 peripheral initialization may conflict. Switching to UART1 avoids this entirely.

3. **Coordinator clarification:** YES, the Coordinator XBee (CE=1) goes on the admin ESP32-CAM. That is correct. Nodes use Router XBees (CE=0, JV=1).

### Solution

Three changes that work together:

1. **Move XBee TX from GPIO 4 → GPIO 3** (repurpose U0RXD)
   - GPIO 3 was Serial Monitor RX (input). We sacrifice Serial input.
   - `Serial.println()` output on GPIO 1 (U0TXD) still works!
   - No flash LED issue on GPIO 3.

2. **Switch from UART2 (Serial2) to UART1 (Serial1)**
   - UART2 defaults to GPIO 16/17 (PSRAM pins on ESP32-CAM)
   - UART1 defaults to GPIO 9/10 (flash chip, but we remap immediately)
   - Avoids any PSRAM-related peripheral conflict.

3. **Set GPIO 4 LOW at boot to disable flash LED**
   - `pinMode(4, OUTPUT); digitalWrite(4, LOW);` at the very start of `setup()`

4. **Add `gpio_reset_pin()` for both TX and RX pins before UART init**
   - Ensures clean GPIO state by detaching from any previous peripheral

### Files to Modify

#### 1. `include/config.h` — Change XBee TX pin

Find the XBee pin section and change XBEE_TX_PIN from 4 to 3:

```cpp
// BEFORE:
#ifdef ESP32CAM_SPI_SD
  #define XBEE_TX_PIN    4   // ESP32 TX → XBee DIN
  #define XBEE_RX_PIN   12   // ESP32 RX ← XBee DOUT
#endif

// AFTER:
#ifdef ESP32CAM_SPI_SD
  #define XBEE_TX_PIN    3   // ESP32 TX → XBee DIN  (repurposed from U0RXD)
  #define XBEE_RX_PIN   12   // ESP32 RX ← XBee DOUT
#endif
```

#### 2. `src/xbee_comm.cpp` — Switch to UART1 and add gpio_reset_pin

**Add include at top:**
```cpp
#include <driver/gpio.h>   // for gpio_reset_pin()
```

**Replace the HardwareSerial declaration with:**
```cpp
// ESP32-CAM: use UART1 — UART2 defaults to GPIO 16/17 which are PSRAM pins
// Generic ESP32: use UART2 (no PSRAM conflict)
#ifdef ESP32CAM_SPI_SD
  static HardwareSerial& xbeeSerial = Serial1;
  #define XBEE_UART_NUM UART_NUM_1
#else
  static HardwareSerial& xbeeSerial = Serial2;
  #define XBEE_UART_NUM UART_NUM_2
#endif
```

**Replace the xbeeInit() function with:**
```cpp
void xbeeInit() {
    // Reset GPIO pins to ensure clean state — detaches from any
    // previous peripheral (SPI, SD_MMC, UART0) before claiming for XBee
    gpio_reset_pin((gpio_num_t)XBEE_TX_PIN);
    gpio_reset_pin((gpio_num_t)XBEE_RX_PIN);

    xbeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);

    // Explicitly route UART signals to our chosen GPIO pins
    uart_set_pin(XBEE_UART_NUM, XBEE_TX_PIN, XBEE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Serial.printf("[XBee] UART%d started (API mode 1) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
                  (XBEE_UART_NUM == UART_NUM_1) ? 1 : 2,
                  XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}
```

**Replace ALL references to `UART_NUM_2` with `XBEE_UART_NUM`** (e.g., in uart_set_pin calls).

#### 3. `src/main.cpp` — Disable flash LED

**Add at the very start of `setup()`, before Serial.begin():**
```cpp
void setup() {
    // Disable ESP32-CAM flash LED immediately (GPIO 4 = flash LED transistor)
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    Serial.begin(115200);
    // ... rest of setup ...
```

### Physical Wiring Change

**Move the wire from GPIO 4 to GPIO 3:**

```
BEFORE:                          AFTER:
ESP32 GPIO 4 → XBee DIN         ESP32 GPIO 3 → XBee DIN
ESP32 GPIO 12 ← XBee DOUT       ESP32 GPIO 12 ← XBee DOUT  (unchanged)
```

**IMPORTANT:** Disconnect the USB-to-serial programming adapter before running
with XBee connected. GPIO 3 is shared between programming and XBee TX — having
both connected causes bus contention and could damage components.

### XBee Module Configuration Reminder

Make sure your XBee modules are configured correctly in XCTU:

| Module | CE | AP | BD | ID | JV |
|--------|----|----|----|----|----|
| **Admin (Coordinator)** | `1` | `1` (API mode) | `3` (9600) | `1234` | — |
| **Node (Router)** | `0` | `1` (API mode) | `3` (9600) | `1234` | `1` |
| **XCTU Test (Router)** | `0` | `1` (API mode) | `3` (9600) | `1234` | `1` |

### Verification

After flashing, the Serial Monitor should show:
```
[XBee] UART1 started (API mode 1) — TX=GPIO3  RX=GPIO12  baud=9600
```

The flash LED should be **OFF**.

If you run the XBee diagnostics from the admin testing page:
- TX Direction should show bytes sent (if you click Send Test)
- RX Direction should show bytes received when the XCTU Router sends data

### Checklist

- [ ] `config.h`: XBEE_TX_PIN = 3 (was 4)
- [ ] `xbee_comm.cpp`: Add `#include <driver/gpio.h>`
- [ ] `xbee_comm.cpp`: Use `Serial1` + `UART_NUM_1` (not Serial2/UART_NUM_2)
- [ ] `xbee_comm.cpp`: Add `gpio_reset_pin()` for both pins in xbeeInit()
- [ ] `xbee_comm.cpp`: Replace all `UART_NUM_2` with `XBEE_UART_NUM`
- [ ] `main.cpp`: Add `pinMode(4, OUTPUT); digitalWrite(4, LOW);` at start of setup()
- [ ] Rewire: XBee DIN from GPIO 4 → GPIO 3
- [ ] Disconnect USB-to-serial adapter before running
- [ ] Build and verify no flash LED, UART1 in log output

---

*End of Change #2*

---

## Change #3: Use UART0 (GPIO 1/3) for XBee — Most Reliable Option

**Date:** 2026-03-07
**Priority:** CRITICAL — replaces Changes #1 and #2 GPIO assignments
**Branch:** `copilot/setup-and-xbee-driver`

### Context

After extensive testing with GPIO 12, 13, 3, 4 — all had conflicts with SD_MMC IOMUX, PSRAM, or the flash LED. **GPIO 1 (U0TXD) and GPIO 3 (U0RXD) are the only pins that reliably work** on ESP32-CAM because they are natively IOMUX-routed to UART0 — no GPIO matrix remapping needed, no conflicts with any other peripheral.

**Trade-off:** USB Serial Monitor is not available. All debug output is disabled at compile time. The XBee serial monitor on the web admin testing page (`/admin/messaging/testing`) replaces the USB Serial Monitor for debugging.

### What to change

#### 1. `config.h` — XBee pin definitions

Change the XBee pin assignments to GPIO 1/3:

```cpp
// XBee S2C (ZigBee)
// ESP32-CAM: Uses UART0 (Serial) on native IOMUX pins.
//   GPIO 1 = U0TXD → XBee DIN  (pin 3 on XBee module)
//   GPIO 3 = U0RXD ← XBee DOUT (pin 2 on XBee module)
//   IOMUX native — no GPIO matrix remapping, no conflicts.
//   USB Serial Monitor is NOT available.
#define XBEE_BAUD     9600
#define XBEE_TX_PIN    1   // U0TXD → XBee DIN (IOMUX native)
#define XBEE_RX_PIN    3   // U0RXD ← XBee DOUT (IOMUX native)
#define XBEE_USES_UART0 1  // UART0 is XBee — serial debug disabled
```

#### 2. `config.h` — Debug macros

Add these macros (used instead of `Serial.printf/println` everywhere):

```cpp
// Debug output macros — disabled when UART0 is used for XBee
#ifdef XBEE_USES_UART0
  #define dbgprintf(...)     do {} while(0)
  #define dbgprintln(x)      do {} while(0)
#else
  #define dbgprintf(...)     Serial.printf(__VA_ARGS__)
  #define dbgprintln(x)      Serial.println(x)
#endif
```

#### 3. `xbee_comm.cpp` — Use UART0 (Serial)

Replace the xbeeSerial reference and xbeeInit:

```cpp
// Use UART0 (Serial) — native IOMUX, most reliable
static HardwareSerial& xbeeSerial = Serial;

void xbeeInit() {
    // UART0 on native GPIO 1/3 — just set baud rate, IOMUX handles routing
    xbeeSerial.begin(XBEE_BAUD);

    dbgprintf("[XBee] UART0 started (API mode 1) — TX=GPIO%d RX=GPIO%d baud=%d\n",
              XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}
```

**Remove:**
- `#include <driver/uart.h>` — not needed (no uart_set_pin)
- `#include <driver/gpio.h>` — not needed (no gpio_reset_pin)
- `gpio_reset_pin()` calls — not needed (IOMUX native)
- `uart_set_pin()` calls — not needed (IOMUX native)

#### 4. `main.cpp` — Don't init Serial for debug

```cpp
void setup() {
    // Disable flash LED
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    // Do NOT call Serial.begin() — UART0 is used by xbeeInit() at 9600 baud

    // 1. SD card
    if (!initSDCard()) {
        while (true) { delay(1000); }
    }

    // ... rest of setup ...

    // XBee init — this calls Serial.begin(9600) internally
    xbeeInit();
}
```

#### 5. ALL source files — Replace Serial.printf/println

Search-and-replace across ALL `.cpp` files:

```
Serial.printf(   →   dbgprintf(
Serial.println(  →   dbgprintln(
```

Files that need this: `main.cpp`, `xbee_comm.cpp`, `sd_storage.cpp`, `node_client.cpp`, `web_server.cpp`, and any others that use Serial.

Also add `#include "config.h"` to any file that doesn't already have it (for the macros).

#### 6. Wiring

```
ESP32 GPIO 1 (U0TXD) ──→ XBee DIN  (pin 3)
ESP32 GPIO 3 (U0RXD) ←── XBee DOUT (pin 2)
ESP32 3.3V            ──→ XBee VCC  (pin 1)
ESP32 GND             ──→ XBee GND  (pin 10)
```

**IMPORTANT:** Disconnect the XBee before uploading firmware. GPIO 1/3 are the USB programming pins.

### Why this works

1. **IOMUX native** — GPIO 1/3 are hardwired to UART0 via IOMUX (not GPIO matrix). IOMUX has highest hardware priority.
2. **No pin conflicts** — SD_MMC doesn't touch GPIO 1/3. PSRAM doesn't use them. No peripheral claims them.
3. **Proven** — UART0 is the most tested and reliable UART peripheral on ESP32.

### Checklist

- [ ] Update `config.h`: XBEE_TX_PIN=1, XBEE_RX_PIN=3, XBEE_USES_UART0=1, add dbg macros
- [ ] Update XBee driver: use `Serial` (UART0), remove gpio_reset_pin/uart_set_pin
- [ ] Remove `Serial.begin(115200)` from setup (or guard with `#ifndef XBEE_USES_UART0`)
- [ ] Replace ALL `Serial.printf()` → `dbgprintf()`, `Serial.println()` → `dbgprintln()` in all files
- [ ] Add `#include "config.h"` to files that need dbgprintf/dbgprintln
- [ ] Rewire: GPIO 1 → XBee DIN, GPIO 3 ← XBee DOUT
- [ ] Build and verify 0 errors/warnings
- [ ] Test: admin serial monitor on web dashboard should show TX/RX activity

---

*End of Change #3*

---

## Change #4: Fix CORE_DEBUG_LEVEL + Atomic Frame Writes (Critical — XBee TX fix)

**Date:** 2026-03-07
**Priority:** CRITICAL — without this, XBee communication does not work AT ALL
**Branch:** `copilot/setup-and-xbee-driver`

### Root Cause

The test project (XBEE_COMM_TEST.md) works perfectly. The admin and node projects don't.
The critical difference:

- **Test project:** `-DCORE_DEBUG_LEVEL=0` (no ESP-IDF logging)
- **Node project:** `-DCORE_DEBUG_LEVEL=3` (INFO-level logging → text output on UART0)

With `CORE_DEBUG_LEVEL=3`, the ESP-IDF WiFi driver, SPI driver, AsyncTCP, and other
internal components emit log messages via `esp_log_write()` which outputs directly to
**UART0** — the SAME UART used for XBee communication.

When `xbeeSendBroadcast()` uses multiple `Serial.write()` calls to build an API frame,
ESP-IDF log messages from Core 0 (WiFi/AsyncTCP) interleave with the frame bytes being
written on Core 1 (Arduino loop). The XBee receives corrupted frames and discards them.

Example of corruption:
```
Expected: 7E 00 1E 10 01 00 00 ... (clean API frame)
Actual:   7E 00 1E 10 49 20 28 77 69 66 69 ... (frame bytes mixed with "I (wifi...")
```

### Three Fixes (all required)

**Fix 1: CORE_DEBUG_LEVEL=0** — Compile out all log output

In `platformio.ini`, change:
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=0          ; <── was 3, MUST be 0
    -DBOARD_HAS_PSRAM=1
    -DESP32CAM_SPI_SD=1
    -DASYNCWEBSERVER_REGEX
```

**Fix 2: Runtime log suppression** — Catch any remaining ESP-IDF internal logs

In `src/main.cpp`, add `#include <esp_log.h>` at the top, then at the very start of
`setup()`, add:

```cpp
#ifdef XBEE_USES_UART0
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
```

Full setup() should be:
```cpp
void setup() {
    // Disable flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

#ifdef XBEE_USES_UART0
    // CRITICAL: suppress ALL serial log output on UART0
    // UART0 is shared between XBee and ESP-IDF log system.
    // Any log output corrupts XBee API frames.
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    Serial.begin(115200);
    delay(500);
#endif

    // 1. XBee — init FIRST, before SD/WiFi/web server
    xbeeInit();

    // 2. SD card
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 3. WiFi access point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 4. DNS captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // 5. Web server
    setupWebServer(server);
    server.begin();

    // 6. Node client + XBee callback
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });
}
```

**Fix 3: Atomic frame writes** — Build entire API frame in one buffer

In `src/xbee_comm.cpp`, replace the `xbeeSendBroadcast()` function. Instead of 6
separate `Serial.write()` calls, build the complete frame in a single buffer and write
it with ONE call:

```cpp
uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE,
        0x00, 0x00
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    // Build complete frame in a single buffer to prevent interleaving
    size_t totalLen = 1 + 2 + 14 + len + 1;
    uint8_t frameBuf[XBEE_MAX_FRAME];
    size_t pos = 0;
    frameBuf[pos++] = XBEE_START_DELIM;
    frameBuf[pos++] = (uint8_t)(frameDataLen >> 8);
    frameBuf[pos++] = (uint8_t)(frameDataLen & 0xFF);
    memcpy(&frameBuf[pos], hdr, 14);
    pos += 14;
    memcpy(&frameBuf[pos], payload, len);
    pos += len;
    frameBuf[pos++] = cksum;

    // Single atomic write — prevents interleaving with ESP-IDF log output
    xbeeSerial.write(frameBuf, totalLen);
    xbeeSerial.flush();

    dbgprintf("[XBee] TX frame ID=%d (%d bytes)\n", fid, (int)len);
    return fid;
}
```

### Why all 3 fixes are needed

| Fix | What it prevents |
|-----|-----------------|
| CORE_DEBUG_LEVEL=0 | Compiles out Arduino `log_e()`, `log_w()`, `log_i()` calls |
| esp_log_level_set() | Catches ESP-IDF internal logs (WiFi stack, lwIP, etc.) |
| Atomic frame write | Defense-in-depth: even if some output leaks, frames aren't corrupted |

### Verification Checklist

- [ ] Change `-DCORE_DEBUG_LEVEL=3` to `-DCORE_DEBUG_LEVEL=0` in `platformio.ini`
- [ ] Add `#include <esp_log.h>` to `src/main.cpp`
- [ ] Add `esp_log_level_set("*", ESP_LOG_NONE)` at start of `setup()`
- [ ] Move `xbeeInit()` to before SD/WiFi init
- [ ] Replace multi-write `xbeeSendBroadcast()` with atomic single-buffer write
- [ ] Build and verify 0 errors/warnings

---

*End of Change #4*

---

## Change #5: Fix Init Order — xbeeInit() AFTER SD Card (Critical)

**Date:** 2026-03-07
**Priority:** CRITICAL — fixes XBee RX not receiving any bytes
**Branch:** `copilot/setup-and-xbee-driver`
**Depends on:** Changes #1, #3, #4

### Problem

The XBee test project (XBEE_COMM_TEST.md, no SD card) communicates
perfectly.  The full node project (with SD card) gets 0 RX bytes.
Same ESP32-CAM, same XBee, same wiring.  The ONLY major hardware
difference is the SD card SPI initialization.

Analysis shows the SPI bus initialization (`spiSD.begin(14,2,15,13)`)
may affect UART0 state when called AFTER `Serial.begin(9600)`.  Even
though they use different pins, the SPI peripheral setup involves
APB bus configuration and interrupt controller changes that can have
side effects on UART0.

### Solution

1. Move `initSDCard()` to BEFORE `xbeeInit()` so UART0 gets the "last word"
2. Add `Serial.end()` before `Serial.begin(9600)` to fully reset UART0
3. Add `delay(2000)` after WiFi for XBee network stabilization
4. Set XBee callback immediately after `xbeeInit()`

### File Changes

#### 1. `src/main.cpp` — New init order

Replace the entire `setup()` function with:

```cpp
void setup() {
    // Disable ESP32-CAM flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

#ifdef XBEE_USES_UART0
    esp_log_level_set("*", ESP_LOG_NONE);
#else
    Serial.begin(115200);
    delay(500);
#endif

    // 1. SD card FIRST — before xbeeInit() so SPI init completes
    //    before we configure UART0
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) delay(1000);
    }

    // 2. WiFi access point
    dbgprintf("[WiFi] Starting AP \"%s\"\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONN);
    delay(100);
    esp_wifi_set_ps(WIFI_PS_NONE);
    dbgprintf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());

    // 3. XBee — init AFTER SD and WiFi so Serial.begin(9600) has the
    //    "last word" on UART0 configuration
    xbeeInit();

    // 4. Node client + XBee callback (immediately after xbeeInit)
    nodeClientInit();
    xbeeSetReceiveCallback([](const char* payload, size_t len) {
        if (!nodeClientHandleCommand(payload, len)) {
            dbgprintf("[XBee] Unhandled: %.80s\n", payload);
        }
    });

    // 5. DNS captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dbgprintf("[DNS] Captive portal running\n");

    // 6. Web server
    setupWebServer(server);
    server.begin();
    dbgprintln("[Web] Server started on port 80");

    // 7. Wait for XBee network to stabilise (coordinator needs 2-4s
    //    to form network, router needs time to join)
    delay(2000);

    dbgprintln("[Node] Setup complete — starting REGISTER cycle");
}
```

#### 2. `src/xbee_comm.cpp` — Reset UART0 before init

Replace `xbeeInit()` with:

```cpp
void xbeeInit() {
    // Fully reset UART0 before reconfiguring.
    // The SD card SPI init and WiFi driver may have affected UART0 state.
    // Calling end() then begin() ensures a clean start.
    xbeeSerial.end();
    delay(10);
    xbeeSerial.begin(XBEE_BAUD);

    dbgprintf("[XBee] UART0 started (API mode 1) — TX=GPIO%d  RX=GPIO%d  baud=%d\n",
              XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}
```

### Verification Checklist

- [ ] Apply changes to `src/main.cpp` and `src/xbee_comm.cpp`
- [ ] Build: `pio run` — 0 errors, 0 warnings
- [ ] Flash to ESP32-CAM
- [ ] Open WiFi `HopFog-Node-01` → `http://192.168.4.1/status`
- [ ] On admin's testing page, check XBee serial monitor for RX bytes > 0

### Debugging: If RX is still 0

If after these changes the admin's diagnostics still show `rx_bytes: 0`:

1. **Check `loop_calls`** in diagnostics — if it's 0, `loop()` isn't running (firmware stuck in setup)
2. **Swap XBee modules** — try the coordinator XBee on the node and vice versa
3. **Check XBee LED** — the XBee's ASSOC LED should blink (network joined). Solid = not joined.
4. **Verify XCTU config** — both modules must have AP=1, same PAN ID (ID), BD=3 (9600)
5. **Verify wiring** — ESP32 GPIO 1 (TX) → XBee DIN (pin 3), ESP32 GPIO 3 (RX) ← XBee DOUT (pin 2)
6. **Test with XCTU** — disconnect ESP32, connect coordinator XBee to USB adapter, use XCTU console to send a test frame. If the router XBee receives it, the XBee network is working.

---

*End of Change #5*
