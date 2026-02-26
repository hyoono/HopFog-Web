#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ───────────────────────────────────────────────
// The ESP32 creates its own WiFi network. Connect to it from your
// phone or laptop, then open http://hopfog.com in a browser.
#define AP_SSID     "HopFog-Network"
#define AP_PASSWORD "changeme123"
#define AP_CHANNEL  1
#define AP_MAX_CONN 8
#define AP_HIDDEN   0       // set to 1 to hide the SSID

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Custom Domain ───────────────────────────────────────────────────
// The ESP32 runs a captive-portal DNS server that resolves ALL domains
// to its own IP, so http://hopfog.com (or any URL) opens the dashboard
// automatically when connected to the ESP32's WiFi network.
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
#define SD_RECIPS_FILE "/db/broadcast_recipients.json"
#define SD_EVENTS_FILE "/db/broadcast_events.json"
#define SD_RES_MSG_FILE "/db/resident_admin_msgs.json"
#define SD_CONVOS_FILE  "/db/conversations.json"
#define SD_DMS_FILE     "/db/direct_messages.json"

// ── XBee S2C (ZigBee) ──────────────────────────────────────────────
// XBee always uses UART2 (Serial2) so UART0 stays free for Serial Monitor.
// ESP32-CAM: GPIO 12 and 13 are free in 1-bit SD_MMC mode.
// Note: GPIO 12 is a boot-strapping pin.  If the ESP32 fails to boot
//       with the XBee connected, disconnect XBee DOUT during power-on
//       or burn the VDD_SDIO efuse to force 3.3 V (one-time, permanent).
#define XBEE_BAUD     9600 // XBee factory default baud rate
#define XBEE_TX_PIN     13   // ESP32 TX → XBee DIN  (pin 3)
#define XBEE_RX_PIN     12   // ESP32 RX ← XBee DOUT (pin 2)

// ── Auth ────────────────────────────────────────────────────────────
#define TOKEN_LENGTH      32
#define MAX_ACTIVE_TOKENS 16

// ── Misc ────────────────────────────────────────────────────────────
// ESP32-CAM has 4 MB PSRAM — allow larger JSON documents.
// Generic ESP32 uses internal RAM only, keep conservative.
#ifdef BOARD_HAS_PSRAM
  #define JSON_DOC_SIZE   16384
#else
  #define JSON_DOC_SIZE    8192
#endif
#define MAX_USERS         50
#define MAX_MESSAGES      200
#define MAX_FOG_DEVICES   20
#define MAX_BROADCASTS    100
#define MAX_CONVERSATIONS 100
#define MAX_DIRECT_MSGS   500
#define MAX_JSON_BODY     4096  // max POST body size for JSON parsing (bytes)

#endif // CONFIG_H
