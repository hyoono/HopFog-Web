/*
 * led_status.h — RGB LED status indicators for HopFog admin/node
 *
 * Activity Status:
 *   RED constant   = device on, no node connected
 *   YELLOW pulsing = connecting/searching
 *   GREEN constant = admin and node connected
 *
 * Battery Status:
 *   RED quick pulse = critically low battery
 *   YELLOW constant = low battery
 *   GREEN constant  = full / done charging
 *   ORANGE constant = charging
 *
 * Uses low-power PWM for energy efficiency.
 *
 * Pin assignment (shared with available GPIOs):
 *   LED_R = GPIO 12  (Red)
 *   LED_G = GPIO 16  (Green — onboard on some ESP32-CAM boards)
 *   LED_B = GPIO 33  (Blue  — onboard status LED on ESP32-CAM)
 *
 * Note: On ESP32-CAM, available GPIO pins are very limited.
 *       GPIO 33 is the built-in status LED (active LOW).
 *       GPIO 12 and GPIO 16 may need external LEDs.
 *       If pins are not available, LED functions are no-ops.
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// LED GPIO pins — adjust for your wiring
#define LED_R_PIN  12   // Red LED
#define LED_G_PIN  16   // Green LED
#define LED_B_PIN  33   // Blue/Status LED (ESP32-CAM built-in, active LOW)

// PWM settings for low power
#define LED_PWM_FREQ  1000
#define LED_PWM_RES   8     // 8-bit (0-255)
#define LED_BRIGHTNESS 30   // low brightness for energy efficiency (0-255)

// LED channels
#define LED_R_CH  4
#define LED_G_CH  5
#define LED_B_CH  6

enum ConnectionStatus {
    CONN_DISCONNECTED,   // RED constant
    CONN_SEARCHING,      // YELLOW pulsing
    CONN_CONNECTED       // GREEN constant
};

/// Initialize LED pins with PWM. Call in setup().
void ledStatusInit();

/// Update connection status LED. Call from loop() for pulsing effects.
void ledStatusUpdate(ConnectionStatus connStatus, int batteryPercent, bool charging);

/// Set a solid LED color (r, g, b each 0-255)
void ledSetColor(uint8_t r, uint8_t g, uint8_t b);

/// Turn off all LEDs
void ledOff();

#endif // LED_STATUS_H
