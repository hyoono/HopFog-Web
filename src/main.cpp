/*
 * HopFog-Web — ESP32 Firmware
 * Main entry point: WiFi AP, SD card, and web-server initialisation.
 *
 * ── Task Architecture ───────────────────────────────────────────────
 * This firmware does NOT create explicit FreeRTOS tasks.  Instead:
 *
 *   Core 0 — WiFi + TCP stack (managed by ESP-IDF)
 *            AsyncTCP task (created internally by ESPAsyncWebServer)
 *
 *   Core 1 — Arduino loop() (DNS processing, XBee serial polling)
 *
 * API request handlers run in the AsyncTCP task context (Core 0).
 * SD card I/O in those handlers is synchronous but brief (~5-30 ms).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "sd_storage.h"
#include "auth.h"
#include "web_server.h"
#include "api_handlers.h"
#include "xbee_comm.h"
#include "node_protocol.h"

AsyncWebServer server(HTTP_PORT);
DNSServer     dnsServer;

// ── WiFi event handler ──────────────────────────────────────────────
static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            dbgprintf("[WiFi] Station connected — total: %d\n",
                      WiFi.softAPgetStationNum());
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            dbgprintf("[WiFi] Station disconnected — total: %d\n",
                      WiFi.softAPgetStationNum());
            break;
        default:
            break;
    }
}

// ── WiFi Access Point ───────────────────────────────────────────────
static void startAP() {
    dbgprintf("[WiFi] Starting AP \"%s\" …\n", AP_SSID);
    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
    delay(100); // let the AP interface stabilise

    // Optimise for short-range reliability:
    // Lower TX power reduces interference & power consumption on ESP32-CAM
    WiFi.setTxPower(WIFI_POWER_17dBm);   // ~50 mW, plenty for indoor AP

    // Disable WiFi power-saving (DTIM buffering) so packets aren't delayed
    esp_wifi_set_ps(WIFI_PS_NONE);

    dbgprintf("[WiFi] AP running — IP: %s  max_conn: %d\n",
              WiFi.softAPIP().toString().c_str(), AP_MAX_CONN);
    dbgprintf("[WiFi] Connect to WiFi \"%s\" (password: %s)\n", AP_SSID, AP_PASSWORD);
}

// ── Setup ───────────────────────────────────────────────────────────
//
//  INIT ORDER — designed to isolate SPI/WiFi from UART0:
//
//   1. Flash LED off
//   2. SD card (SPI — before UART0 is configured at 9600)
//   3. Auth
//   4. delay(2000) — let bootloader UART0 traffic settle
//   5. xbeeInit() — Serial.begin(9600)
//   6. xbeeQueryConfig() — AT probe (proves UART works)
//   7. Node protocol + callback
//   8. WiFi AP
//   9. DNS + Web server
//  10. delay(3000) — XBee network stabilisation
//
void setup() {
    // Disable ESP32-CAM flash LED (GPIO 4 = flash LED transistor)
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

#ifndef XBEE_USES_UART0
    // Generic ESP32: UART0 is free for debug output
    Serial.begin(115200);
    delay(500);
#endif
    dbgprintln("\n========================================");
    dbgprintln("   HopFog-Web  ESP32 Firmware");
    dbgprintln("========================================");

    // 1. SD card — init SPI BEFORE configuring UART0 for XBee.
    //    This ensures any SPI GPIO matrix changes happen while UART0
    //    is still at bootloader baud rate (115200), not at XBee baud.
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) { delay(1000); }
    }

    // 2. Auth
    authInit();

    // 3. Wait for bootloader UART0 output and XBee boot-up to finish.
    //    The ESP32 bootloader outputs text at 115200 baud on GPIO1.
    //    The XBee module takes ~1.5s to boot and join a network.
    //    This delay matches the test project's blinkLed(3, 300) = 1.8s.
    delay(2000);

    // 4. XBee — Serial.begin(9600) AFTER all SPI/peripheral init
    xbeeInit();

    // 5. AT command probe — query XBee module config
    //    If this gets responses, UART TX+RX are both proven working.
    xbeeQueryConfig();

    // 6. Node protocol + callback
    nodeProtocolInit();
    xbeeSetReceiveCallback([](const char* line, size_t len) {
        if (!nodeProtocolHandleLine(line, len)) {
            dbgprintf("[XBee] RX (%d bytes): %s\n", (int)len, line);
        }
    });

    // 7. WiFi AP
    startAP();

    // 8. DNS captive portal + Web server
    dnsServer.setTTL(300);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dbgprintf("[DNS] Captive portal → %s\n", WiFi.softAPIP().toString().c_str());

    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();
    dbgprintf("[HTTP] Server on port %d — http://%s\n", HTTP_PORT, CUSTOM_DOMAIN);

    // 9. Wait for XBee ZigBee network to form/join (3 seconds)
    delay(3000);
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    yield(); // allow background tasks (WiFi, TCP) to run without blocking
}
