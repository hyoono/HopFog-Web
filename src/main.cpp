/*
 * HopFog-Web — ESP32 Firmware
 * Main entry point: WiFi, SD card, and web-server initialisation.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

#include "config.h"
#include "sd_storage.h"
#include "auth.h"
#include "web_server.h"
#include "api_handlers.h"

AsyncWebServer server(HTTP_PORT);

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

    // 4. NTP time sync
    syncTime();

    // 5. Web server (static files + API)
    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();
    Serial.printf("[HTTP] Server listening on port %d\n", HTTP_PORT);
}

// ── Loop ────────────────────────────────────────────────────────────
void loop() {
    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Lost connection – reconnecting …");
        connectWiFi();
    }
    delay(1000);
}
