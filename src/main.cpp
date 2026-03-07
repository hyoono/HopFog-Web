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
void setup() {
    // Disable ESP32-CAM flash LED immediately (GPIO 4 = flash LED transistor)
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

#ifndef XBEE_USES_UART0
    // Only init Serial for debug output if UART0 is NOT used for XBee
    Serial.begin(115200);
    delay(500);
#endif
    dbgprintln("\n========================================");
    dbgprintln("   HopFog-Web  ESP32 Firmware");
    dbgprintln("========================================");

    // 1. SD card
    if (!initSDCard()) {
        dbgprintln("[FATAL] SD card init failed – halting.");
        while (true) { delay(1000); }
    }

    // 2. Auth subsystem
    authInit();

    // 3. WiFi access point
    startAP();

    // 4. Captive-portal DNS — resolve ALL domains to the AP IP
    dnsServer.setTTL(300);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dbgprintf("[DNS] Captive portal active — http://%s → %s\n",
              CUSTOM_DOMAIN, WiFi.softAPIP().toString().c_str());

    // 5. Web server (static files + API)
    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();
    dbgprintf("[HTTP] Server listening on port %d\n", HTTP_PORT);
    dbgprintf("[HTTP] Open http://%s in your browser\n", CUSTOM_DOMAIN);

    // 6. XBee S2C (ZigBee) communication + node protocol
    xbeeInit();
    nodeProtocolInit();
    xbeeSetReceiveCallback([](const char* line, size_t len) {
        // Try to handle as JSON node command first
        if (!nodeProtocolHandleLine(line, len)) {
            // Not a JSON command — log as raw data
            dbgprintf("[XBee] RX (%d bytes): %s\n", (int)len, line);
        }
    });
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    yield(); // allow background tasks (WiFi, TCP) to run without blocking
}
