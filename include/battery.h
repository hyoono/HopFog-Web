/*
 * battery.h — INA219 battery monitoring for HopFog admin/node
 *
 * Uses I2C on GPIO 14 (SDA) and GPIO 15 (SCL).
 * NOTE: These pins are shared with SD SPI on ESP32-CAM.
 * The INA219 is initialized AFTER SD card init completes.
 * If INA219 is not detected, all functions return safe defaults.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

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
    int      percentage;    // 0-100%
    BatteryStatus status;
};

/// Initialize INA219 sensor. Call AFTER SD card init.
/// Returns true if sensor detected.
bool batteryInit();

/// Read current battery state. Fast (~1ms I2C read).
BatteryInfo batteryRead();

/// Get a human-readable status string
const char* batteryStatusStr(BatteryStatus s);

#endif // BATTERY_H
