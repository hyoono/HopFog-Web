/*
 * HopFog-Web — ESP32-CAM Firmware
 * Main entry point
 *
 * Setup order matches the working XBEE_COMM_TEST project:
 *   1. Silence ALL UART0 output (ets_printf, esp_log)
 *   2. Flash LED off
 *   3. LED blink delay (~1.8s) — matches test project timing
 *   4. xbeeInit()  — Serial.begin(9600)
 *   5. Receive callback
 *   6. SD card init
 *   7. Auth
 *   8. WiFi AP
 *   9. DNS + Web server
 *  10. delay(3000) — XBee network formation
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

// Silence ets_printf (ROM/bootloader/ESP-IDF internal prints on UART0)
#include <rom/ets_sys.h>

#include "config.h"
#include "sd_storage.h"
#include "auth.h"
#include "web_server.h"
#include "api_handlers.h"
#include "xbee_comm.h"
#include "node_protocol.h"

AsyncWebServer server(HTTP_PORT);
DNSServer     dnsServer;

// ── LED helpers (match test project) ────────────────────────────────
#define LED_PIN       33   // ESP32-CAM built-in red LED (active LOW)
#define FLASH_LED_PIN  4   // ESP32-CAM flash LED

static void ledOn()  { digitalWrite(LED_PIN, LOW); }
static void ledOff() { digitalWrite(LED_PIN, HIGH); }
static void blinkLed(int n, int ms) {
    for (int i = 0; i < n; i++) { ledOn(); delay(ms); ledOff(); delay(ms); }
}

// ── No-op putc to silence ets_printf ────────────────────────────────
static void nullPutc(char c) { (void)c; }

// ── Setup ───────────────────────────────────────────────────────────
void setup() {
    // STEP 0: SILENCE ALL UART0 OUTPUT
    //
    // ets_printf() — used internally by WiFi driver, SPI driver, and
    // AsyncTCP — bypasses both CORE_DEBUG_LEVEL and esp_log_level_set().
    // These prints corrupt XBee API frames on shared UART0.
    // This is the key fix that the working test project didn't need
    // because it had no SD card or complex WiFi configuration.
    ets_install_putc1(nullPutc);
    esp_log_level_set("*", ESP_LOG_NONE);

    // Step 1: Flash LED off (GPIO 4)
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    // Step 2: Status LED blink — 3×300ms = ~1.8 seconds
    //         This delay matches the test project EXACTLY.
    //         It gives the bootloader time to finish ALL UART0 output
    //         before we reconfigure UART0 to 9600 baud for XBee.
    pinMode(LED_PIN, OUTPUT);
    ledOff();
    blinkLed(3, 300);

    // Step 3: XBee — Serial.begin(9600)
    xbeeInit();

    // Step 4: Receive callback
    xbeeSetReceiveCallback([](const char* line, size_t len) {
        if (!nodeProtocolHandleLine(line, len)) {
            logMsg('R', "Unhandled: %.160s", line);
        }
    });

    // Step 5: SD card
    if (!initSDCard()) {
        logMsg('E', "SD card init failed!");
        // Don't halt — admin can still work without SD for XBee testing
    }

    // Step 6: Auth
    authInit();

    // Step 7: WiFi AP (simple — matches test project style)
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
    delay(100);

    logMsg('S', "WiFi AP: %s  IP: 192.168.4.1", AP_SSID);

    // Step 8: DNS captive portal + Web server
    dnsServer.setTTL(300);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    nodeProtocolInit();
    setupWebServer(server);
    registerApiRoutes(server);
    server.begin();

    logMsg('S', "Web server on port %d", HTTP_PORT);

    // Step 9: Wait for XBee network (matches test project)
    delay(3000);
    blinkLed(5, 100);

    logMsg('S', "Setup complete. Waiting for XBee traffic...");
}

// ── Loop (matches test project + calls nodeProtocolLoop for PING) ────
void loop() {
    dnsServer.processNextRequest();
    xbeeProcessIncoming();
    nodeProtocolLoop();
    delay(10);  // Match test project. yield() alone doesn't provide enough
                // time for the WiFi/TCP stack on Core 0 to process packets.
}
