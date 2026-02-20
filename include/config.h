#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Configuration ──────────────────────────────────────────────
// Update these with your network credentials
#define WIFI_SSID     "HopFog-Network"
#define WIFI_PASSWORD "changeme123"

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── SD Card SPI Pins (default ESP32 VSPI) ───────────────────────────
#define SD_CS_PIN   5
// MOSI = GPIO 23, MISO = GPIO 19, CLK = GPIO 18 (defaults)

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
