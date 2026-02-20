# Deployment Guide — HopFog-Web ESP32

Complete step-by-step instructions for deploying HopFog-Web to an ESP32
microcontroller with an SD card.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Hardware Setup](#2-hardware-setup)
3. [Software Setup](#3-software-setup)
4. [Configure WiFi](#4-configure-wifi)
5. [Prepare the SD Card](#5-prepare-the-sd-card)
6. [Build the Firmware](#6-build-the-firmware)
7. [Flash the ESP32](#7-flash-the-esp32)
8. [Verify Deployment](#8-verify-deployment)
9. [Troubleshooting](#9-troubleshooting)
10. [Updating After Changes](#10-updating-after-changes)

---

## 1. Prerequisites

### Hardware

| Item | Notes |
|------|-------|
| ESP32 development board | Any ESP-WROOM-32 board (e.g., DevKitC, NodeMCU-32S) |
| Micro-SD card module | SPI-based breakout (e.g., HW-125 or built-in slot) |
| Micro-SD card | FAT32 formatted, ≥ 1 GB |
| USB cable | Micro-USB or USB-C depending on your board |
| Jumper wires | 6 wires for SD card module (if not built-in) |

### Software

| Tool | Install Command |
|------|----------------|
| Python 3.8+ | [python.org](https://www.python.org/downloads/) |
| PlatformIO CLI | `pip install platformio` |
| Git | [git-scm.com](https://git-scm.com/) |

> **Tip:** You can also use the [PlatformIO IDE extension for VS Code](https://platformio.org/install/ide?install=vscode)
> instead of the CLI.

---

## 2. Hardware Setup

Wire the SD card module to the ESP32 using the default VSPI bus:

```
SD Module        ESP32
─────────        ─────
CS       ──────► GPIO 5
MOSI     ──────► GPIO 23
MISO     ──────► GPIO 19
CLK      ──────► GPIO 18
VCC      ──────► 3.3V
GND      ──────► GND
```

> **Important:** Use 3.3V, not 5V. Most SD modules have a built-in regulator,
> but check your module's documentation.

If your ESP32 board has a built-in SD card slot, no wiring is needed — just
verify the CS pin matches `SD_CS_PIN` in `include/config.h` (default: GPIO 5).

---

## 3. Software Setup

```bash
# 1. Clone the repository
git clone https://github.com/hyoono/HopFog-Web.git
cd HopFog-Web

# 2. Install PlatformIO
pip install platformio

# 3. Verify installation
pio --version
```

---

## 4. Configure WiFi

Edit `include/config.h` and replace the placeholder WiFi credentials:

```c
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

> **Note:** The ESP32 only supports 2.4 GHz WiFi, not 5 GHz.

### Optional: Change the SD card CS pin

If your SD module uses a different chip-select pin:

```c
#define SD_CS_PIN   5    // Change to your pin number
```

---

## 5. Prepare the SD Card

### Option A: Use the helper script (Linux/macOS)

```bash
# Insert SD card and find its mount point
# Linux:  usually /media/$USER/<CARD_NAME>
# macOS:  usually /Volumes/<CARD_NAME>

./scripts/prepare_sd.sh /path/to/sd/mount
```

### Option B: Manual copy

1. Format the SD card as **FAT32**
2. Copy the entire `data/sd/www/` folder to the SD card root
3. Create an empty `db/` folder on the SD card

The SD card should look like this:

```
SD Card Root/
├── www/
│   ├── login.html
│   ├── register.html
│   ├── dashboard.html
│   ├── users.html
│   ├── logs.html
│   ├── fog_nodes.html
│   ├── settings.html
│   ├── admin_messaging.html
│   ├── admin_broadcasts.html
│   ├── admin_broadcast_detail.html
│   ├── admin_sos.html
│   ├── admin_queue.html
│   ├── admin_tracking.html
│   ├── admin_testing.html
│   ├── css/
│   │   └── styles.css
│   ├── js/
│   │   └── scripts.js
│   └── images/
│       ├── HopFog-Logo.png
│       └── error-404-monochrome.svg
└── db/                ← created automatically on first boot
```

4. Safely eject the SD card
5. Insert it into the ESP32's SD card module

---

## 6. Build the Firmware

### Option A: Use the deploy script

```bash
# Build only
./scripts/deploy.sh build
```

### Option B: Use PlatformIO directly

```bash
pio run
```

A successful build prints something like:

```
Building .pio/build/esp32dev/firmware.bin
RAM:   [==        ]  15.2% (used 49812 bytes from 327680 bytes)
Flash: [======    ]  58.3% (used 764321 bytes from 1310720 bytes)
========================= [SUCCESS] =========================
```

---

## 7. Flash the ESP32

1. Connect the ESP32 to your computer via USB
2. Run one of:

### Option A: Deploy script (build + flash + monitor)

```bash
./scripts/deploy.sh
```

### Option B: PlatformIO commands

```bash
# Flash the firmware
pio run --target upload

# Open serial monitor to see output
pio device monitor
```

### Specifying a serial port

If auto-detection fails, specify the port manually:

```bash
# Linux
./scripts/deploy.sh all --port /dev/ttyUSB0

# macOS
./scripts/deploy.sh all --port /dev/cu.usbserial-0001

# Windows (in PlatformIO terminal)
pio run --target upload --upload-port COM3
```

---

## 8. Verify Deployment

After flashing, the serial monitor should show:

```
========================================
   HopFog-Web  ESP32 Firmware
========================================
[SD] Initialising SD card …
[SD] Mounted — Total: 3728 MB, Used: 1 MB
[SD] Created directory: /db
[Auth] Initialised
[WiFi] Connecting to YourNetworkName...
[WiFi] Connected — IP: 192.168.1.42
[NTP] Time: 2026-02-20 04:30:00 UTC
[HTTP] Server listening on port 80
```

Open a browser on the same network and navigate to:

```
http://192.168.1.42
```

(Replace with the actual IP shown in the serial monitor.)

You should see the HopFog login page. Register an admin account to get started.

---

## 9. Troubleshooting

### SD card not detected

```
[FATAL] SD card init failed – halting.
```

**Fix:**
- Check wiring (CS → GPIO 5, MOSI → 23, MISO → 19, CLK → 18)
- Ensure the card is formatted as FAT32 (not exFAT or NTFS)
- Try a different SD card
- Verify `SD_CS_PIN` in `config.h` matches your wiring

### WiFi connection timeout

```
[WiFi] Connection timeout – restarting …
```

**Fix:**
- Verify SSID and password in `include/config.h` (they are case-sensitive)
- Ensure you're using a 2.4 GHz network (ESP32 does not support 5 GHz)
- Move the ESP32 closer to the router
- Check that the router allows new device connections

### Upload fails / ESP32 not found

```
Error: Could not open port /dev/ttyUSB0
```

**Fix:**
- Check the USB cable (some are charge-only with no data)
- Install the USB-to-serial driver for your board:
  - **CP2102**: [Silicon Labs driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
  - **CH340**: [CH340 driver](http://www.wch-ic.com/downloads/CH341SER_ZIP.html)
- On Linux, add yourself to the `dialout` group: `sudo usermod -aG dialout $USER` (then log out/in)
- On macOS, check System Settings → Privacy & Security for blocked drivers

### Web pages show "File not found"

**Fix:**
- Verify the SD card has the `www/` folder at the root level
- Re-run `./scripts/prepare_sd.sh /path/to/sd`
- Check that files were not placed inside a subfolder (e.g., `sd/www/` instead of `www/`)

### "Invalid email or password" on login

**Fix:**
- Register a new admin account first at `/register`
- If you've forgotten the password, delete `/db/users.json` from the SD card and re-register

### Build errors

```
Error: Library not found
```

**Fix:**
- Run `pio pkg install` to download dependencies
- Check your internet connection
- Try `pio run --target clean` then `pio run`

---

## 10. Updating After Changes

After modifying code or web files:

### Code changes (C++ source)

```bash
# Rebuild and flash
./scripts/deploy.sh
```

### Web UI changes (HTML/CSS/JS)

No firmware reflash needed — just update the SD card:

```bash
# Option 1: Use the script
./scripts/prepare_sd.sh /path/to/sd/mount

# Option 2: Manually copy changed files to SD card www/ folder
```

Then power-cycle the ESP32 (or press the reset button).

### Keeping the database

The `db/` folder on the SD card contains your user accounts, messages, and
device registrations. When updating web files, only replace the `www/` folder —
leave `db/` untouched to preserve your data.

---

## Quick Reference

| Task | Command |
|------|---------|
| Build firmware | `pio run` or `./scripts/deploy.sh build` |
| Flash ESP32 | `pio run --target upload` or `./scripts/deploy.sh flash` |
| Serial monitor | `pio device monitor` or `./scripts/deploy.sh monitor` |
| Build + flash + monitor | `./scripts/deploy.sh` |
| Prepare SD card | `./scripts/prepare_sd.sh /path/to/sd` |
| Clean build | `pio run --target clean` |
| Install dependencies | `pio pkg install` |
