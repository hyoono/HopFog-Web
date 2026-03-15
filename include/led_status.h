/*
 * led_status.h — Flash LED status indicator for HopFog ESP32-CAM
 *
 * Uses the built-in flash LED on GPIO 4 for status indication.
 * No external LED required — works on every ESP32-CAM board.
 *
 * GPIO 4 is time-shared with INA219 I2C SDA (see battery.h).
 * When a battery read occurs (~2ms every 5s), the LED briefly suspends.
 *
 * Patterns (single white LED):
 *   OFF              = no nodes registered (idle)
 *   Slow pulse       = searching for nodes
 *   Solid dim glow   = connected to node(s)
 *   Fast blink (10Hz)= critical battery (<5%)
 *   Double blink     = low battery (5-15%)
 *   Slow breathe     = charging
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// Flash LED on GPIO 4 (built-in on ESP32-CAM, active HIGH via MOSFET)
#define FLASH_LED_PIN     4
#define FLASH_LED_CHANNEL 4   // LEDC channel
#define FLASH_LED_FREQ    1000
#define FLASH_LED_RES     8   // 8-bit (0-255)
#define FLASH_LED_DIM     8   // Very dim glow (~3% duty)
#define FLASH_LED_MED     20  // Medium for blink patterns
#define FLASH_LED_BRIGHT  40  // Brighter for attention (still low)

enum ConnectionStatus {
    CONN_DISCONNECTED,   // OFF
    CONN_SEARCHING,      // Slow pulse
    CONN_CONNECTED       // Solid dim glow
};

/// Initialize flash LED with PWM. Call in setup().
void ledStatusInit();

/// Update flash LED pattern. Call from loop() every ~200ms.
void ledStatusUpdate(ConnectionStatus connStatus, int batteryPercent, bool charging);

/// Set flash LED brightness (0-255). Active HIGH.
void ledSetBrightness(uint8_t val);

/// Turn off flash LED
void ledOff();

#endif // LED_STATUS_H
