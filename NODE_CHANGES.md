# HopFog-Node — Complete Rebuild Instructions

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
> Branch: `copilot/setup-and-xbee-driver`
>
> This document SUPERSEDES all previous change instructions (#1–#9).
> It describes a complete rebuild of the XBee driver and init sequence.

---

## What Changed on the Admin Side (HopFog-Web)

The admin firmware was rebuilt from scratch to match the working
XBEE_COMM_TEST project (two ESP32-CAMs communicating via XBee).
The test project works perfectly; the full admin/node projects didn't.

### Critical fix: `ets_install_putc1(nullPutc)`

ESP-IDF components (WiFi driver, SPI driver, AsyncTCP) internally use
`ets_printf()` which outputs to UART0 **regardless** of `CORE_DEBUG_LEVEL`
or `esp_log_level_set()`. These prints corrupt XBee API frames on UART0.

The fix: install a no-op character output function at the very start
of `setup()` to swallow ALL ROM/bootloader/ESP-IDF UART0 output.

### Other changes

| Change | Why |
|--------|-----|
| Removed esp32dev environment | ESP32-CAM only target |
| Removed ALL `#ifdef` conditionals | No code path ambiguity |
| Hardcoded `Serial` for XBee | UART0 on GPIO 1/3, no indirection |
| LED blink before xbeeInit | ~1.8s delay matches test project timing |
| Simple `WiFi.softAP()` | No `setTxPower()`, no `esp_wifi_set_ps()` |
| `delay(10)` in loop | Matches test project, prevents 100% CPU spin |
| SD init doesn't halt on fail | Admin still works for XBee testing without SD |

---

## Required Changes for the Node

Apply ALL of the following changes to the HopFog-Node repository.

### 1. `platformio.ini`

Replace the entire file:

```ini
; HopFog-Node — ESP32-CAM firmware
[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino
monitor_speed = 115200
lib_deps =
    me-no-dev/ESPAsyncWebServer@^1.2.4
    me-no-dev/AsyncTCP@^1.1.1
    bblanchon/ArduinoJson@^7.3.0
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM=1
```

**Key:** `CORE_DEBUG_LEVEL=0` — MUST be 0, not 3. Remove any other envs.

### 2. `include/config.h`

Remove ALL `#ifdef` conditionals. Hardcode for ESP32-CAM:

- XBee: `XBEE_TX_PIN=1`, `XBEE_RX_PIN=3`, `XBEE_BAUD=9600`
- SD card: SPI on HSPI — `CS=13, CLK=14, MISO=2, MOSI=15`
- Debug macros: `#define dbgprintf(...)  do {} while(0)` (always disabled)

### 3. `src/xbee_comm.cpp`

The XBee driver must use `Serial` directly (not `Serial2`, not `xbeeSerial`
reference). No `#ifdef` conditionals. Match this structure:

```cpp
void xbeeInit() {
    Serial.begin(9600);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    // Build 0x10 TX Request frame
    // Write with 6 separate Serial.write() calls (delimiter, len, hdr, payload, checksum)
    // Call Serial.flush() after
}

void xbeeProcessIncoming() {
    while (Serial.available()) {
        uint8_t b = Serial.read();
        // State machine: WAIT_DELIM → GOT_LEN_HI → GOT_LEN_LO → READING_DATA → GOT_CHECKSUM
        // Handle 0x90 (RX packet), 0x8B (TX status), 0x8A (Modem status), 0x10 (self-echo)
    }
}
```

### 4. `src/main.cpp` — CRITICAL

The init order MUST be:

```cpp
#include <rom/ets_sys.h>
#include <esp_log.h>

static void nullPutc(char c) { (void)c; }

void setup() {
    // STEP 0: Silence ALL UART0 output — THIS IS THE KEY FIX
    ets_install_putc1(nullPutc);
    esp_log_level_set("*", ESP_LOG_NONE);

    // Step 1: Flash LED off
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    // Step 2: LED blink delay (~1.8 seconds)
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);  // LED off (active LOW)
    for (int i = 0; i < 3; i++) {
        digitalWrite(33, LOW); delay(300);
        digitalWrite(33, HIGH); delay(300);
    }

    // Step 3: XBee init — Serial.begin(9600)
    xbeeInit();
    xbeeSetReceiveCallback(onXBeeReceive);

    // Step 4: SD card
    initSDCard();

    // Step 5: WiFi AP (simple — no setTxPower, no esp_wifi_set_ps)
    WiFi.mode(WIFI_AP);
    WiFi.softAP("HopFog-Node", "changeme123");
    delay(100);

    // Step 6: Web server
    setupWebServer();

    // Step 7: Wait for XBee network
    delay(3000);
}

void loop() {
    xbeeProcessIncoming();
    // ... node client state machine ...
    delay(10);
}
```

### 5. `src/sd_storage.cpp`

SD card init must use SPI mode (NOT SD_MMC):

```cpp
#include <SD.h>
#include <SPI.h>

bool initSDCard() {
    static SPIClass spiSD(HSPI);
    spiSD.begin(14, 2, 15, 13);  // CLK, MISO, MOSI, CS
    if (!SD.begin(13, spiSD)) {
        return false;
    }
    // ... seed files ...
    return true;
}
```

### 6. XBee Configuration (XCTU)

Node XBee modules must be configured as:
- **CE = 0** (Router, NOT Coordinator)
- **JV = 1** (Join Verification enabled)
- **AP = 1** (API mode 1)
- **BD = 3** (9600 baud)
- **ID = same PAN ID as admin**

Admin XBee = Coordinator (CE=1). Node XBees = Router (CE=0).

---

## Verification Checklist

After flashing, connect to the node's WiFi AP and check the XBee
diagnostics page:

1. ✅ `totalRxBytes > 0` — UART0 RX is working
2. ✅ `xbTxStatusOk > 0` — XBee is responding to TX frames
3. ✅ `xbRxFramesParsed > 0` — Remote device data received
4. ✅ Node auto-registers with admin (sends REGISTER, gets REGISTER_ACK)

If `totalRxBytes == 0`:
- Check wiring: XBee DOUT → GPIO 3 (pin 2 → pin U0RXD)
- Check XBee power: 3.3V (NOT 5V)
- Check AP mode: AP must be 1 (not 0) in XCTU
- Verify `ets_install_putc1(nullPutc)` is the FIRST line in setup()
