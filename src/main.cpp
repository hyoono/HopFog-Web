/*
 * HopFog-Web — ESP32 Firmware
 * Main entry point: WiFi, SD card, and web-server initialisation.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

#include "config.h"
#include "sd_storage.h"
#include "auth.h"
#include "web_server.h"
#include "api_handlers.h"

AsyncWebServer server(HTTP_PORT);
DNSServer     dnsServer;

// ── WiFi ────────────────────────────────────────────────────────────
static void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - start > 15000) {
            Serial.println("\n[WiFi] Connection timeout – restarting …");
            ESP.restart();
        }
    }
    Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── NTP Time Sync ───────────────────────────────────────────────────
static void syncTime() {
    Serial.println("[NTP] Syncing time …");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm ti;
    int attempts = 0;
    while (!getLocalTime(&ti) && attempts < 10) {
        delay(500);
        attempts++;
    }
    if (attempts < 10) {
        Serial.printf("[NTP] Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                      ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.println("[NTP] Time sync failed — timestamps will use uptime");
    }
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

    // 3. WiFi
    connectWiFi();

    // 4. DNS server — resolve hopfog.com to this device's IP
    dnsServer.setTTL(300);
    dnsServer.start(DNS_PORT, CUSTOM_DOMAIN, WiFi.localIP());
    Serial.printf("[DNS] Resolving %s → %s\n",
                  CUSTOM_DOMAIN, WiFi.localIP().toString().c_str());
    Serial.printf("[DNS] Point your device's DNS to %s to use http://%s\n",
                  WiFi.localIP().toString().c_str(), CUSTOM_DOMAIN);

    // 5. NTP time sync
    syncTime();

    // 6. Web server (static files + API)
    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();
    Serial.printf("[HTTP] Server listening on port %d\n", HTTP_PORT);
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    // Process DNS requests
    dnsServer.processNextRequest();

    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Lost connection – reconnecting …");
        connectWiFi();
    }
    delay(10);
}
