# HopFog-Web — ESP32 Firmware

Web-based admin dashboard for the HopFog IoT/Emergency Communication System,
built as an ESP32 firmware with SD card storage.

> **📖 Full deployment guide:** See **[DEPLOY.md](DEPLOY.md)** for detailed
> step-by-step instructions, wiring diagrams, and troubleshooting.

## How to Deploy (TL;DR)

### Windows + ESP32-CAM

```cmd
pip install platformio

REM Edit include\config.h with your WiFi SSID and password

REM Copy web files to a FAT32-formatted SD card (e.g. drive E:)
scripts\prepare_sd.bat E:

REM Insert SD card into ESP32-CAM, then build + flash + monitor
scripts\deploy.bat all esp32cam
```

### Linux / macOS + Generic ESP32

```bash
pip install platformio

# Edit include/config.h with your WiFi SSID and password

# Copy web files to a FAT32-formatted SD card
./scripts/prepare_sd.sh /path/to/sd/card

# Insert SD card into ESP32, then build + flash + monitor
./scripts/deploy.sh
```

After boot the serial monitor shows the IP address — open it in a browser.

## Hardware Requirements

### Option A: ESP32-CAM (AI-Thinker) — recommended, no wiring needed

| Component | Notes |
|-----------|-------|
| ESP32-CAM board | AI-Thinker module (built-in SD card slot) |
| Micro-SD card | FAT32 formatted, ≥ 1 GB |
| USB-to-serial adapter | FTDI or CP2102 (for programming — unless board has USB) |

The ESP32-CAM has a built-in micro-SD card slot. No extra wiring is needed.
The camera module is **not used** by this firmware.

### Option B: Generic ESP32 + external SD card module

| Component | Notes |
|-----------|-------|
| ESP32 dev board | Any ESP-WROOM-32 based board |
| Micro-SD card module | SPI interface (CS → GPIO 5) |
| Micro-SD card | FAT32 formatted, ≥ 1 GB recommended |

Default SPI wiring (generic ESP32 only — not needed for ESP32-CAM):

| SD module pin | ESP32 GPIO |
|---------------|-----------|
| CS | 5 |
| MOSI | 23 |
| MISO | 19 |
| CLK | 18 |
| VCC | 3.3 V |
| GND | GND |

## Quick Start

### 1. Install PlatformIO

```bash
pip install platformio
# or install the PlatformIO IDE extension for VS Code
```

### 2. Configure WiFi

Edit `include/config.h` and set your network credentials:

```c
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

### 3. Prepare the SD Card

Format the SD card as **FAT32**, then copy the web files to the card:

**Windows:**
```cmd
scripts\prepare_sd.bat E:
```

**Linux / macOS:**
```bash
./scripts/prepare_sd.sh /media/$USER/SD_CARD
```

Or manually copy the contents of `data/sd/` to the card root:

```
SD Card Root/
├── www/            ← web pages, CSS, JS, images
│   ├── login.html
│   ├── dashboard.html
│   ├── users.html
│   ├── logs.html
│   ├── fog_nodes.html
│   ├── settings.html
│   ├── register.html
│   ├── admin_messaging.html
│   ├── admin_broadcasts.html
│   ├── admin_broadcast_detail.html
│   ├── admin_sos.html
│   ├── admin_queue.html
│   ├── admin_tracking.html
│   ├── admin_testing.html
│   ├── css/styles.css
│   ├── js/scripts.js
│   └── images/
│       ├── HopFog-Logo.png
│       └── error-404-monochrome.svg
└── db/             ← created automatically on first boot
    ├── users.json
    ├── messages.json
    ├── fog_devices.json
    ├── broadcasts.json
    └── resident_admin_msgs.json
```

### 4. Build & Flash

**Windows + ESP32-CAM:**
```cmd
REM Build + flash + monitor in one step
scripts\deploy.bat all esp32cam
```

**Linux / macOS + generic ESP32:**
```bash
./scripts/deploy.sh
```

**Or use PlatformIO directly:**
```bash
# Generic ESP32
pio run
pio run --target upload
pio device monitor

# ESP32-CAM
pio run -e esp32cam
pio run -e esp32cam --target upload
pio device monitor
```

### 5. Access the Dashboard

After boot, the serial monitor will print the ESP32's IP address:

```
[WiFi] Connected — IP: 192.168.1.42
[HTTP] Server listening on port 80
```

Open `http://<ESP32-IP>` in a browser to access the admin dashboard.

## Project Structure

```
├── platformio.ini          # PlatformIO build configuration
├── DEPLOY.md               # Full deployment guide & troubleshooting
├── scripts/
│   ├── deploy.sh           # Build + flash + monitor (Linux/macOS)
│   ├── deploy.bat          # Build + flash + monitor (Windows)
│   ├── prepare_sd.sh       # Copy web files to SD card (Linux/macOS)
│   └── prepare_sd.bat      # Copy web files to SD card (Windows)
├── include/
│   ├── config.h            # WiFi, pin, and limit constants
│   ├── sd_storage.h        # SD card JSON data operations
│   ├── auth.h              # Token-based authentication
│   ├── web_server.h        # Static file serving
│   └── api_handlers.h      # REST API endpoint handlers
├── src/
│   ├── main.cpp            # Entry point (WiFi + SD + server init)
│   ├── sd_storage.cpp      # JSON read/write on SD card
│   ├── auth.cpp            # Password hashing + session tokens
│   ├── web_server.cpp      # Static file routes from SD card
│   └── api_handlers.cpp    # REST API routes (login, users, etc.)
├── data/sd/                # Files to copy to the SD card
│   └── www/                # Web UI (HTML, CSS, JS, images)
├── app/                    # Original Python source (reference)
├── database/               # Original Python source (reference)
├── routes/                 # Original Python source (reference)
├── services/               # Original Python source (reference)
├── templates/              # Original Jinja2 templates (reference)
└── static/                 # Original static assets (reference)
```

## Architecture

- **Web server**: [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) — async HTTP on ESP32
- **Data storage**: JSON files on SD card via ArduinoJson
- **Authentication**: SHA-256 password hashing (mbedtls) + in-memory session tokens
- **Frontend**: Static HTML served from SD card; client-side JavaScript fetches data from REST API endpoints

### Key API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/login` | Admin login (form) |
| POST | `/register` | Admin registration (form) |
| POST | `/forgot-password` | Password reset |
| GET | `/api/dashboard` | Dashboard stats |
| GET | `/api/users` | List all users |
| POST | `/api/admin/create-mobile-user` | Create mobile user |
| PUT | `/api/users/{id}/toggle-status` | Activate/deactivate user |
| GET | `/api/messages` | List messages |
| DELETE | `/api/messages/{id}` | Delete message |
| GET | `/api/fog-devices` | List fog devices |
| POST | `/api/fog-devices/register` | Register fog device |
| POST | `/api/fog-devices/{id}/status` | Update device status |
| GET | `/api/broadcasts` | List broadcasts |
| POST | `/api/broadcasts` | Create broadcast |
| POST | `/api/change-password` | Change password |
| POST | `/api/mobile/login` | Mobile app login |
| GET | `/api/resident-admin/messages` | List resident→admin messages |
| POST | `/api/resident-admin/messages` | Create resident→admin message |

## Original Python Version

The original FastAPI/Python source code is preserved in `app/`, `database/`,
`routes/`, `services/`, `templates/`, and `static/` directories for reference.
To run the original version:

```bash
python -m venv env
source env/bin/activate
pip install fastapi uvicorn sqlalchemy python-dotenv bcrypt pyjwt
uvicorn app.main:app --reload
```

