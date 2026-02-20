#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Configuration ──────────────────────────────────────────────
// Update these with your network credentials
#define WIFI_SSID     "HopFog-Network"
#define WIFI_PASSWORD "changeme123"

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Custom Domain ───────────────────────────────────────────────────
// The ESP32 runs a local DNS server so you can access it at
// http://hopfog.com instead of using the IP address.
// Devices must use the ESP32's IP as their DNS server (see DEPLOY.md).
// Note: while this DNS is active, the real hopfog.com (if it exists)
// will be unreachable from devices using this ESP32 as their DNS.
#define CUSTOM_DOMAIN "hopfog.com"
#define DNS_PORT      53

// ── SD Card Configuration ───────────────────────────────────────────
//
// Two modes are supported:
//
// 1. SPI mode (default) — for generic ESP32 + external SD card module
//    Wiring: CS→GPIO 5, MOSI→23, MISO→19, CLK→18
//
// 2. SD_MMC 1-bit mode — for ESP32-CAM with built-in SD card slot
//    Enabled automatically when building with: pio run -e esp32cam
//    No wiring needed (on-board slot uses GPIO 2, 14, 15)
//
#ifdef USE_SD_MMC
  // ESP32-CAM built-in SD slot — 1-bit SD_MMC mode
  // GPIO 4 is the on-board flash LED; keep it OFF to avoid SD conflicts
  #define ESP32CAM_FLASH_PIN  4
#else
  // Generic ESP32 + external SPI SD module
  #define SD_CS_PIN   5
  // MOSI = GPIO 23, MISO = GPIO 19, CLK = GPIO 18 (defaults)
#endif

// ── SD Card Paths ───────────────────────────────────────────────────
#define SD_WWW_DIR     "/www"
#define SD_DB_DIR      "/db"
#define SD_USERS_FILE  "/db/users.json"
#define SD_MSGS_FILE   "/db/messages.json"
#define SD_FOG_FILE    "/db/fog_devices.json"
#define SD_BCASTS_FILE "/db/broadcasts.json"
#define SD_RES_MSG_FILE "/db/resident_admin_msgs.json"

// ── Auth ────────────────────────────────────────────────────────────
#define TOKEN_LENGTH      32
#define MAX_ACTIVE_TOKENS 16

// ── Misc ────────────────────────────────────────────────────────────
#define JSON_DOC_SIZE     8192
#define MAX_USERS         50
#define MAX_MESSAGES      200
#define MAX_FOG_DEVICES   20
#define MAX_BROADCASTS    100

#endif // CONFIG_H
