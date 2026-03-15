/*
 * led_status.cpp — Flash LED status indicator for HopFog ESP32-CAM
 *
 * Single white LED on GPIO 4 with PWM brightness control.
 * Active HIGH (MOSFET driver on ESP32-CAM board).
 *
 * GPIO 4 is time-shared with INA219 I2C SDA — the battery module
 * briefly detaches/reattaches LEDC every 5 seconds for I2C reads.
 */

#include "led_status.h"

static bool ledInitialized = false;
static unsigned long lastPulseMs = 0;
static bool pulseUp = true;
static uint8_t pulseVal = 0;

void ledStatusInit() {
    ledcSetup(FLASH_LED_CHANNEL, FLASH_LED_FREQ, FLASH_LED_RES);
    ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);
    ledOff();
    ledInitialized = true;
}

void ledSetBrightness(uint8_t val) {
    if (!ledInitialized) return;
    ledcWrite(FLASH_LED_CHANNEL, val);  // Active HIGH: 0=off, 255=full
}

void ledOff() {
    if (!ledInitialized) return;
    ledcWrite(FLASH_LED_CHANNEL, 0);
}

void ledStatusUpdate(ConnectionStatus connStatus, int batteryPercent, bool charging) {
    if (!ledInitialized) return;

    unsigned long now = millis();

    // Battery critical: fast blink (10Hz) — highest priority
    if (batteryPercent >= 0 && batteryPercent < 5) {
        if (now - lastPulseMs > 50) {  // 50ms on/off = 10Hz
            lastPulseMs = now;
            pulseUp = !pulseUp;
            ledSetBrightness(pulseUp ? FLASH_LED_BRIGHT : 0);
        }
        return;
    }

    // Charging: slow breathe effect
    if (charging) {
        if (now - lastPulseMs > 40) {
            lastPulseMs = now;
            if (pulseUp) {
                pulseVal += 1;
                if (pulseVal >= FLASH_LED_MED) pulseUp = false;
            } else {
                if (pulseVal == 0) pulseUp = true;
                else pulseVal -= 1;
            }
            ledSetBrightness(pulseVal);
        }
        return;
    }

    // Low battery: double blink pattern (1-second cycle)
    if (batteryPercent >= 0 && batteryPercent < 15) {
        unsigned long phase = (now / 100) % 10;
        if (phase == 0 || phase == 1 || phase == 3 || phase == 4) {
            ledSetBrightness(FLASH_LED_MED);
        } else {
            ledOff();
        }
        return;
    }

    // Connection status
    switch (connStatus) {
        case CONN_DISCONNECTED:
            ledOff();
            break;

        case CONN_SEARCHING:
            // Slow pulse (breathe): ~2-second cycle
            if (now - lastPulseMs > 30) {
                lastPulseMs = now;
                if (pulseUp) {
                    pulseVal += 1;
                    if (pulseVal >= FLASH_LED_DIM) pulseUp = false;
                } else {
                    if (pulseVal == 0) pulseUp = true;
                    else pulseVal -= 1;
                }
                ledSetBrightness(pulseVal);
            }
            break;

        case CONN_CONNECTED:
            ledSetBrightness(FLASH_LED_DIM);
            break;
    }
}
