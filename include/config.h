#ifndef CONFIG_H
#define CONFIG_H

// ═══════════════════════════════════════════════════════════════════
//  HopFog-Web — ESP32-CAM Configuration (ONLY target platform)
// ═══════════════════════════════════════════════════════════════════

// ── WiFi Access Point ───────────────────────────────────────────────
#define AP_SSID     "HopFog-Network"
#define AP_PASSWORD "changeme123"
#define AP_CHANNEL  6
#define AP_MAX_CONN 8
#define AP_HIDDEN   0

// ── Web Server ──────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Custom Domain (captive portal) ──────────────────────────────────
#define CUSTOM_DOMAIN "hopfog.com"
#define DNS_PORT      53

// ── SD Card (SPI mode on ESP32-CAM built-in slot) ───────────────────
//
// The ESP32-CAM's SD card slot is hardware-wired to these pins.
// We use SPI mode (NOT SD_MMC) to avoid GPIO 12/13 IOMUX conflicts.
//
#define SD_CS_PIN       13   // SD DAT3/CS
#define SD_SPI_CLK      14   // SD CLK
#define SD_SPI_MISO      2   // SD DAT0/MISO
#define SD_SPI_MOSI     15   // SD CMD/MOSI

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

// ── XBee S2C (ZigBee) on UART0 ─────────────────────────────────────
//
// ESP32-CAM uses UART0 native pins (IOMUX, no GPIO matrix remapping):
//   GPIO 1 (U0TXD) → XBee DIN  (pin 3 on XBee module)
//   GPIO 3 (U0RXD) ← XBee DOUT (pin 2 on XBee module)
//   3.3V            → XBee VCC  (pin 1)
//   GND             → XBee GND  (pin 10)
//
// TRADE-OFF: USB Serial Monitor is NOT available.
//            Use the web dashboard at http://192.168.4.1 for diagnostics.
//            Disconnect XBee before uploading new firmware.
//
#define XBEE_BAUD     9600
#define XBEE_TX_PIN   1     // U0TXD → XBee DIN
#define XBEE_RX_PIN   3     // U0RXD ← XBee DOUT

// ── Debug output (always disabled — UART0 = XBee) ──────────────────
#define dbgprintf(...)     do {} while(0)
#define dbgprintln(x)      do {} while(0)

// ── Auth ────────────────────────────────────────────────────────────
#define TOKEN_LENGTH      32
#define MAX_ACTIVE_TOKENS 16

// ── Misc ────────────────────────────────────────────────────────────
#define JSON_DOC_SIZE   16384   // ESP32-CAM has 4 MB PSRAM
#define MAX_USERS         50
#define MAX_MESSAGES      200
#define MAX_FOG_DEVICES   20
#define MAX_BROADCASTS    100
#define MAX_CONVERSATIONS 100
#define MAX_DIRECT_MSGS   500
#define MAX_JSON_BODY     4096

#endif // CONFIG_H
