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
            Serial.printf("[WiFi] Station connected — total: %d\n",
                          WiFi.softAPgetStationNum());
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.printf("[WiFi] Station disconnected — total: %d\n",
                          WiFi.softAPgetStationNum());
            break;
        default:
            break;
    }
}

// ── WiFi Access Point ───────────────────────────────────────────────
static void startAP() {
    Serial.printf("[WiFi] Starting AP \"%s\" …\n", AP_SSID);
    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
    delay(100); // let the AP interface stabilise

    // Optimise for short-range reliability:
    // Lower TX power reduces interference & power consumption on ESP32-CAM
    WiFi.setTxPower(WIFI_POWER_17dBm);   // ~50 mW, plenty for indoor AP

    // Disable WiFi power-saving (DTIM buffering) so packets aren't delayed
    esp_wifi_set_ps(WIFI_PS_NONE);

    Serial.printf("[WiFi] AP running — IP: %s  max_conn: %d\n",
                  WiFi.softAPIP().toString().c_str(), AP_MAX_CONN);
    Serial.printf("[WiFi] Connect to WiFi \"%s\" (password: %s)\n", AP_SSID, AP_PASSWORD);
}

// ── Setup ───────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("   HopFog-Web  ESP32 Firmware");
    Serial.println("========================================");

    // 1. SD card
    if (!initSDCard()) {
        Serial.println("[FATAL] SD card init failed – halting.");
        while (true) { delay(1000); }
    }

    // 2. Auth subsystem
    authInit();

    // 3. WiFi access point
    startAP();

    // 4. Captive-portal DNS — resolve ALL domains to the AP IP
    dnsServer.setTTL(300);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.printf("[DNS] Captive portal active — http://%s → %s\n",
                  CUSTOM_DOMAIN, WiFi.softAPIP().toString().c_str());

    // 5. Web server (static files + API)
    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();
    Serial.printf("[HTTP] Server listening on port %d\n", HTTP_PORT);
    Serial.printf("[HTTP] Open http://%s in your browser\n", CUSTOM_DOMAIN);

    // 6. XBee S2C (ZigBee) communication + node protocol
    xbeeInit();
    nodeProtocolInit();
    xbeeSetReceiveCallback([](const uint8_t* data, size_t len,
                              uint32_t sHi, uint32_t sLo) {
        // Try to handle as JSON node command first
        if (!nodeProtocolHandleData(data, len, sHi, sLo)) {
            // Not a JSON command — log as raw data
            Serial.printf("[XBee] RX from %08X%08X (%d bytes): ", sHi, sLo, (int)len);
            Serial.write(data, len);
            Serial.println();
        }
    });
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    yield(); // allow background tasks (WiFi, TCP) to run without blocking
}
