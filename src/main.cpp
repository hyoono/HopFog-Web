/*
 * HopFog-Web — ESP32 Firmware
 * Main entry point: WiFi AP, SD card, and web-server initialisation.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "sd_storage.h"
#include "auth.h"
#include "web_server.h"
#include "api_handlers.h"

AsyncWebServer server(HTTP_PORT);
DNSServer     dnsServer;

// ── WiFi Access Point ───────────────────────────────────────────────
static void startAP() {
    Serial.printf("[WiFi] Starting AP \"%s\" …\n", AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
    delay(100); // let the AP interface stabilise
    Serial.printf("[WiFi] AP running — IP: %s\n", WiFi.softAPIP().toString().c_str());
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
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    dnsServer.processNextRequest();
    delay(10);
}
