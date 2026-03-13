/*
 * led_status.cpp — RGB LED status indicators for HopFog
 *
 * DISABLED BY DEFAULT on ESP32-CAM: GPIO 25/26 are camera pins
 * not broken out on the AI-Thinker ESP32-CAM header.
 *
 * To enable: set -DENABLE_LED=1 in platformio.ini build_flags
 * and wire RGB LED to GPIO 25 (R), GPIO 26 (G), GPIO 33 (B).
 *
 * When disabled, all LED functions are safe no-ops.
 */

#include "led_status.h"
#include "config.h"

static bool ledInitialized = false;

#if ENABLE_LED
static unsigned long lastPulseMs = 0;
static bool pulseUp = true;
static uint8_t pulseVal = 0;

static uint8_t capBrightness(uint8_t val) {
    return (val > 0) ? min((uint8_t)LED_BRIGHTNESS, val) : 0;
}
#endif

void ledStatusInit() {
#if ENABLE_LED
    ledcSetup(LED_R_CH, LED_PWM_FREQ, LED_PWM_RES);
    ledcSetup(LED_G_CH, LED_PWM_FREQ, LED_PWM_RES);
    ledcSetup(LED_B_CH, LED_PWM_FREQ, LED_PWM_RES);

    ledcAttachPin(LED_R_PIN, LED_R_CH);
    ledcAttachPin(LED_G_PIN, LED_G_CH);
    ledcAttachPin(LED_B_PIN, LED_B_CH);

    ledOff();
    ledInitialized = true;
    dbgprintln("[LED] Status LEDs initialized");
#else
    // No LED hardware available on this board
    ledInitialized = false;
#endif
}

void ledSetColor(uint8_t r, uint8_t g, uint8_t b) {
#if ENABLE_LED
    if (!ledInitialized) return;
    ledcWrite(LED_R_CH, capBrightness(r));
    ledcWrite(LED_G_CH, capBrightness(g));
    // GPIO 33 is active LOW on ESP32-CAM
    ledcWrite(LED_B_CH, 255 - capBrightness(b));
#else
    (void)r; (void)g; (void)b;
#endif
}

void ledOff() {
#if ENABLE_LED
    if (!ledInitialized) return;
    ledcWrite(LED_R_CH, 0);
    ledcWrite(LED_G_CH, 0);
    ledcWrite(LED_B_CH, 255); // active LOW
#endif
}

void ledStatusUpdate(ConnectionStatus connStatus, int batteryPercent, bool charging) {
#if ENABLE_LED
    if (!ledInitialized) return;

    unsigned long now = millis();

    // Battery takes priority if critical
    if (batteryPercent >= 0 && batteryPercent < 5) {
        if (now - lastPulseMs > 100) {
            lastPulseMs = now;
            pulseUp = !pulseUp;
            ledSetColor(pulseUp ? LED_BRIGHTNESS : 0, 0, 0);
        }
        return;
    }

    if (charging) {
        ledSetColor(LED_BRIGHTNESS, LED_BRIGHTNESS / 3, 0);
        return;
    }

    if (batteryPercent >= 0 && batteryPercent < 15) {
        ledSetColor(LED_BRIGHTNESS, LED_BRIGHTNESS / 2, 0);
        return;
    }

    switch (connStatus) {
        case CONN_DISCONNECTED:
            ledSetColor(LED_BRIGHTNESS, 0, 0);
            break;
        case CONN_SEARCHING:
            if (now - lastPulseMs > 30) {
                lastPulseMs = now;
                if (pulseUp) {
                    pulseVal += 2;
                    if (pulseVal >= LED_BRIGHTNESS) pulseUp = false;
                } else {
                    if (pulseVal < 2) { pulseVal = 0; pulseUp = true; }
                    else pulseVal -= 2;
                }
                ledSetColor(pulseVal, pulseVal / 2, 0);
            }
            break;
        case CONN_CONNECTED:
            ledSetColor(0, LED_BRIGHTNESS, 0);
            break;
    }
#else
    (void)connStatus; (void)batteryPercent; (void)charging;
#endif
}
