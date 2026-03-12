/*
 * led_status.cpp — RGB LED status indicators for HopFog
 *
 * Uses LEDC PWM for smooth pulsing and low power consumption.
 * All brightness values capped at LED_BRIGHTNESS for energy efficiency.
 */

#include "led_status.h"
#include "config.h"

static bool ledInitialized = false;
static unsigned long lastPulseMs = 0;
static bool pulseUp = true;
static uint8_t pulseVal = 0;

void ledStatusInit() {
    // Configure LEDC PWM channels
    ledcSetup(LED_R_CH, LED_PWM_FREQ, LED_PWM_RES);
    ledcSetup(LED_G_CH, LED_PWM_FREQ, LED_PWM_RES);
    ledcSetup(LED_B_CH, LED_PWM_FREQ, LED_PWM_RES);

    ledcAttachPin(LED_R_PIN, LED_R_CH);
    ledcAttachPin(LED_G_PIN, LED_G_CH);
    ledcAttachPin(LED_B_PIN, LED_B_CH);

    ledOff();
    ledInitialized = true;
    dbgprintln("[LED] Status LEDs initialized");
}

void ledSetColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!ledInitialized) return;
    // Cap brightness
    r = (r > 0) ? min((uint8_t)LED_BRIGHTNESS, r) : 0;
    g = (g > 0) ? min((uint8_t)LED_BRIGHTNESS, g) : 0;
    b = (b > 0) ? min((uint8_t)LED_BRIGHTNESS, b) : 0;

    ledcWrite(LED_R_CH, r);
    ledcWrite(LED_G_CH, g);
    // GPIO 33 is active LOW on ESP32-CAM
    ledcWrite(LED_B_CH, 255 - b);
}

void ledOff() {
    if (!ledInitialized) return;
    ledcWrite(LED_R_CH, 0);
    ledcWrite(LED_G_CH, 0);
    ledcWrite(LED_B_CH, 255); // active LOW
}

void ledStatusUpdate(ConnectionStatus connStatus, int batteryPercent, bool charging) {
    if (!ledInitialized) return;

    unsigned long now = millis();

    // Battery takes priority if critical
    if (batteryPercent >= 0 && batteryPercent < 5) {
        // Quick pulsing red for critically low battery
        if (now - lastPulseMs > 100) {
            lastPulseMs = now;
            pulseUp = !pulseUp;
            ledSetColor(pulseUp ? LED_BRIGHTNESS : 0, 0, 0);
        }
        return;
    }

    if (charging) {
        // Orange constant = charging
        ledSetColor(LED_BRIGHTNESS, LED_BRIGHTNESS / 3, 0);
        return;
    }

    if (batteryPercent >= 0 && batteryPercent < 15) {
        // Yellow constant = low battery
        ledSetColor(LED_BRIGHTNESS, LED_BRIGHTNESS / 2, 0);
        return;
    }

    // Connection status
    switch (connStatus) {
        case CONN_DISCONNECTED:
            // RED constant
            ledSetColor(LED_BRIGHTNESS, 0, 0);
            break;

        case CONN_SEARCHING:
            // YELLOW pulsing
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
            // GREEN constant
            ledSetColor(0, LED_BRIGHTNESS, 0);
            break;
    }
}
