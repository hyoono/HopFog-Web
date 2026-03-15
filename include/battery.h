/*
 * battery.h — INA219 battery monitoring for HopFog ESP32-CAM
 *
 * I2C wiring (uses available exposed GPIO pins):
 *   INA219 SDA → GPIO 4 (flash LED pin — time-shared with LED PWM)
 *   INA219 SCL → GPIO 0 (free after boot)
 *   INA219 VCC → 3.3V
 *   INA219 GND → GND
 *
 * GPIO 4 (SDA) is time-shared with the flash LED:
 *   - Flash LED PWM runs 99.99% of the time for status indication
 *   - Every 5 seconds, LED briefly suspends (~2ms) for I2C read
 *   - Imperceptible to human eye
 *
 * GPIO 0 notes:
 *   - Strapping pin (HIGH = normal boot, LOW = download mode)
 *   - INA219 module's I2C pull-up keeps it HIGH → helps normal boot
 *   - Free for I2C use after boot completes
 *
 * When INA219 is not connected: batteryInit() returns false,
 * batteryRead() returns safe defaults. No I2C activity occurs.
 * The flash LED works normally for connection status indication.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// I2C pins for INA219 — time-shared with flash LED on GPIO 4
#define BAT_SDA_PIN  4   // GPIO 4 (shared with flash LED)
#define BAT_SCL_PIN  0   // GPIO 0 (free after boot, pull-up = normal boot)

// Battery thresholds (for a 1S LiPo / 18650 cell)
#define BATTERY_FULL_MV       4200  // 4.2V = 100%
#define BATTERY_LOW_MV        3400  // 3.4V = ~15%
#define BATTERY_CRITICAL_MV   3200  // 3.2V = ~5%
#define BATTERY_EMPTY_MV      3000  // 3.0V = 0% (cutoff)

// Battery status enum for LED indicators
enum BatteryStatus {
    BAT_UNKNOWN,     // INA219 not detected
    BAT_FULL,        // > 3.8V
    BAT_NORMAL,      // 3.4V - 3.8V
    BAT_LOW,         // 3.2V - 3.4V
    BAT_CRITICAL,    // < 3.2V
    BAT_CHARGING     // current is negative (charging)
};

struct BatteryInfo {
    bool     available;     // true if INA219 detected
    float    voltage_V;     // bus voltage in volts
    float    current_mA;    // current in mA (positive = discharging)
    float    power_mW;      // power in mW
    int      percentage;    // 0-100% (-1 = not available)
    BatteryStatus status;
};

/// Initialize INA219 sensor. Call AFTER ledStatusInit().
/// Briefly suspends flash LED for I2C probe (~5ms).
/// Returns true if sensor detected.
bool batteryInit();

/// Read current battery state. Briefly suspends flash LED (~2ms).
/// Only call from main loop (not from web server callbacks).
BatteryInfo batteryRead();

/// Get the last cached battery reading. Thread-safe — call from anywhere.
BatteryInfo batteryGetCached();

/// Get a human-readable status string
const char* batteryStatusStr(BatteryStatus s);

#endif // BATTERY_H
