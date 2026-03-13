/*
 * led_status.h — RGB LED status indicators for HopFog admin/node
 *
 * DISABLED BY DEFAULT: ESP32-CAM (AI-Thinker) does not expose the
 * required GPIO pins (25, 26) on its header.
 *
 * To enable: add -DENABLE_LED=1 to platformio.ini build_flags
 * and wire an RGB LED to GPIO 25 (R), 26 (G), 33 (B).
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
 * Pin assignment (when enabled — uses free camera pins):
 *   LED_R = GPIO 25  (Red)
 *   LED_G = GPIO 26  (Green)
 *   LED_B = GPIO 33  (Blue — built-in status LED, active LOW)
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// LED GPIO pins — using free camera pins (camera not used in this project)
// CRITICAL: GPIO 16 = PSRAM CS, GPIO 12/14/15 = SD card — NEVER use!
#define LED_R_PIN  25   // Red   (was camera VSYNC)
#define LED_G_PIN  26   // Green (was camera SIOD)
#define LED_B_PIN  33   // Blue  (ESP32-CAM built-in, active LOW)

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
