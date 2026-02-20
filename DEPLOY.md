# Deployment Guide — HopFog-Web ESP32

Complete step-by-step instructions for deploying HopFog-Web to an ESP32
microcontroller with an SD card.

**Supported boards:**
- **ESP32-CAM** (AI-Thinker) — built-in SD card slot, no extra wiring
- **Generic ESP32** + external SPI SD card module

**Supported OS:** Windows, Linux, macOS

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

**Option A — ESP32-CAM (recommended):**

| Item | Notes |
|------|-------|
| ESP32-CAM board | AI-Thinker module with built-in SD card slot |
| Micro-SD card | FAT32 formatted, ≥ 1 GB |
| USB-to-serial adapter | FTDI or CP2102 (3.3 V) — unless your board has USB-C |
| Jumper wires | 4 wires for programming (see wiring below) |

**Option B — Generic ESP32 + external SD module:**

| Item | Notes |
|------|-------|
| ESP32 development board | Any ESP-WROOM-32 board (e.g., DevKitC, NodeMCU-32S) |
| Micro-SD card module | SPI-based breakout (e.g., HW-125 or built-in slot) |
| Micro-SD card | FAT32 formatted, ≥ 1 GB |
| USB cable | Micro-USB or USB-C depending on your board |
| Jumper wires | 6 wires for SD card module |

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

### ESP32-CAM (AI-Thinker)

The SD card slot is **built in** — no SD wiring needed.

The camera module is present but **not used** by this firmware. You can leave
it attached or remove it.

**Programming wiring** (with a USB-to-serial adapter like FTDI/CP2102):

```
USB-to-Serial        ESP32-CAM
─────────────        ─────────
TX           ──────► U0R  (GPIO 3)
RX           ──────► U0T  (GPIO 1)
GND          ──────► GND
3.3V (or 5V) ──────► 5V
                     GPIO 0 ──► GND  (only during upload, then remove)
```

> **To flash:** connect GPIO 0 to GND, press the reset button, upload, then
> disconnect GPIO 0 from GND and press reset again to run normally.
>
> If your ESP32-CAM board has a USB-C port (e.g. ESP32-CAM-MB), just plug
> it in — no adapter or GPIO 0 jumper needed.

### Generic ESP32 + external SD card module

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

> **Windows note:** Use Command Prompt or PowerShell. If `pio` is not found
> after install, try `python -m platformio` or restart your terminal.

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

### Option A: Use the helper script

**Windows:**
```cmd
REM Insert SD card — check which drive letter Windows assigns (e.g. E:)
scripts\prepare_sd.bat E:
```

**Linux / macOS:**
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

**Windows + ESP32-CAM:**
```cmd
scripts\deploy.bat build esp32cam
```

**Linux / macOS + generic ESP32:**
```bash
./scripts/deploy.sh build
```

### Option B: Use PlatformIO directly

```bash
# Generic ESP32
pio run

# ESP32-CAM
pio run -e esp32cam
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
2. **ESP32-CAM only:** connect GPIO 0 to GND before pressing reset (skip if using USB-C board)
3. Run one of:

### Option A: Deploy script (build + flash + monitor)

**Windows + ESP32-CAM:**
```cmd
scripts\deploy.bat all esp32cam
```

**Linux / macOS:**
```bash
./scripts/deploy.sh
```

### Option B: PlatformIO commands

```bash
# Generic ESP32
pio run --target upload
pio device monitor

# ESP32-CAM
pio run -e esp32cam --target upload
pio device monitor
```

### Specifying a serial port

If auto-detection fails, specify the port manually:

```bash
# Windows
pio run -e esp32cam --target upload --upload-port COM3
pio device monitor --port COM3

# Linux
./scripts/deploy.sh all --port /dev/ttyUSB0

# macOS
./scripts/deploy.sh all --port /dev/cu.usbserial-0001
```

> **ESP32-CAM note:** After flashing, disconnect GPIO 0 from GND and press
> the reset button to boot normally.

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
[DNS] Resolving hopfog.com → 192.168.1.42
[DNS] Point your device's DNS to 192.168.1.42 to use http://hopfog.com
[NTP] Time: 2026-02-20 04:30:00 UTC
[HTTP] Server listening on port 80
```

### Access via http://hopfog.com

The ESP32 runs a local DNS server that resolves `hopfog.com` to its own IP.
To use this, **set your device's DNS server to the ESP32's IP address**:

**Windows:**
1. Open **Settings → Network & Internet → Wi-Fi → your network → Edit**
2. Under DNS server assignment, switch to **Manual**
3. Set Preferred DNS to the ESP32's IP (e.g. `192.168.1.42`)
4. Save, then open `http://hopfog.com` in your browser

**macOS:**
1. Open **System Settings → Wi-Fi → your network → Details → DNS**
2. Add the ESP32's IP as the first DNS server
3. Open `http://hopfog.com` in your browser

**Linux:**
1. Configure your network manager's DNS settings to add the ESP32's IP
   as the primary DNS server, or temporarily edit `/etc/resolv.conf`:
   `nameserver 192.168.1.42` (use the ESP32's actual IP)
   > Note: direct edits to `/etc/resolv.conf` may be overwritten by
   > `systemd-resolved` or NetworkManager. Use `nmcli` or your desktop
   > network settings for a persistent change.
2. Open `http://hopfog.com` in your browser

**Or** you can always use the IP address directly (e.g. `http://192.168.1.42`).

> **Tip:** The domain can be changed in `include/config.h` (`CUSTOM_DOMAIN`).

You should see the HopFog login page. Register an admin account to get started.

---

## 9. Troubleshooting

### SD card not detected

```
[FATAL] SD card init failed – halting.
```

**Fix (ESP32-CAM):**
- Make sure you built with the `esp32cam` environment: `pio run -e esp32cam`
- Push the SD card firmly into the slot until it clicks
- Ensure the card is formatted as FAT32 (not exFAT or NTFS)
- Try a different SD card (some high-capacity cards have compatibility issues)

**Fix (generic ESP32):**
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
or on Windows:
```
Error: Could not open port COM3
```

**Fix (Windows):**
- Open Device Manager → Ports (COM & LPT) to find the correct COM port
- Install the USB-to-serial driver for your adapter:
  - **CP2102**: [Silicon Labs driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
  - **CH340**: [CH340 driver](http://www.wch-ic.com/downloads/CH341SER_ZIP.html)
  - **FTDI**: [FTDI driver](https://ftdichip.com/drivers/)
- Check the USB cable (some are charge-only with no data lines)
- Close any other serial monitors (only one program can use a COM port)

**Fix (Linux):**
- Add yourself to the `dialout` group: `sudo usermod -aG dialout $USER` (then log out/in)
- Check the cable

**Fix (macOS):**
- Check System Settings → Privacy & Security for blocked drivers

### ESP32-CAM won't enter flash mode

**Fix:**
- Ensure GPIO 0 is connected to GND **before** pressing reset
- Press the reset button on the ESP32-CAM while GPIO 0 is grounded
- Try a shorter USB cable or a powered USB hub
- If using the ESP32-CAM-MB (USB-C daughter board), no GPIO 0 jumper is needed

### Web pages show "File not found"

**Fix:**
- Verify the SD card has the `www/` folder at the root level
- Re-run the prepare script: `scripts\prepare_sd.bat E:` (Windows) or `./scripts/prepare_sd.sh /path/to/sd` (Linux/macOS)
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

### `pio` command not found (Windows)

**Fix:**
- Try `python -m platformio` instead of `pio`
- Close and reopen your terminal after installing PlatformIO
- Make sure Python's `Scripts` directory is in your system PATH

### `hopfog.com` doesn't work in the browser

**Fix:**
- Make sure your computer is on the **same WiFi network** as the ESP32
- Set your device's DNS server to the ESP32's IP address (see step 8 above)
- If changing DNS is not an option, use the IP address directly (e.g. `http://192.168.1.42`)
- To change the domain, edit `CUSTOM_DOMAIN` in `include/config.h`

---

## 10. Updating After Changes

After modifying code or web files:

### Code changes (C++ source)

```bash
# Generic ESP32
./scripts/deploy.sh                      # Linux/macOS

# ESP32-CAM on Windows
scripts\deploy.bat all esp32cam
```

### Web UI changes (HTML/CSS/JS)

No firmware reflash needed — just update the SD card:

```cmd
REM Windows
scripts\prepare_sd.bat E:
```
```bash
# Linux/macOS
./scripts/prepare_sd.sh /path/to/sd/mount
```

Then power-cycle the ESP32 (or press the reset button).

### Keeping the database

The `db/` folder on the SD card contains your user accounts, messages, and
device registrations. When updating web files, only replace the `www/` folder —
leave `db/` untouched to preserve your data.

---

## Quick Reference

| Task | Linux/macOS | Windows |
|------|-------------|---------|
| Build (generic) | `pio run` | `pio run` |
| Build (ESP32-CAM) | `pio run -e esp32cam` | `pio run -e esp32cam` |
| Flash (generic) | `pio run --target upload` | `pio run --target upload` |
| Flash (ESP32-CAM) | `pio run -e esp32cam --target upload` | `scripts\deploy.bat flash esp32cam` |
| Serial monitor | `pio device monitor` | `pio device monitor` |
| Full deploy | `./scripts/deploy.sh` | `scripts\deploy.bat all esp32cam` |
| Prepare SD card | `./scripts/prepare_sd.sh /media/…` | `scripts\prepare_sd.bat E:` |
| Clean build | `pio run --target clean` | `pio run --target clean` |
| Install deps | `pio pkg install` | `pio pkg install` |
