/*
 * battery.cpp — INA219 battery monitoring for HopFog ESP32-CAM
 *
 * Time-shares GPIO 4 (flash LED) with I2C SDA.
 * GPIO 0 used as I2C SCL (free after boot).
 *
 * When INA219 is not connected: batteryInit() returns false,
 * batteryRead() returns safe defaults without any I2C activity.
 * The flash LED works normally for connection status indication.
 */

#include "battery.h"
#include "led_status.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 ina219;
static bool ina219Available = false;
static BatteryInfo cachedReading = {false, 0, 0, 0, -1, BAT_UNKNOWN};

static int voltageToPercent(float mv) {
    if (mv >= BATTERY_FULL_MV) return 100;
    if (mv <= BATTERY_EMPTY_MV) return 0;
    return (int)((mv - BATTERY_EMPTY_MV) * 100.0 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

bool batteryInit() {
    // Briefly take GPIO 4 from LED to I2C for sensor detection
    ledcDetachPin(FLASH_LED_PIN);
    Wire.begin(BAT_SDA_PIN, BAT_SCL_PIN);

    if (!ina219.begin(&Wire)) {
        Wire.end();
        ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);
        ina219Available = false;
        return false;
    }

    ina219.setCalibration_16V_400mA();
    ina219Available = true;

    Wire.end();
    ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);

    return true;
}

BatteryInfo batteryRead() {
    BatteryInfo info;
    info.available = ina219Available;

    if (!ina219Available) {
        info.voltage_V  = 0;
        info.current_mA = 0;
        info.power_mW   = 0;
        info.percentage = -1;
        info.status     = BAT_UNKNOWN;
        cachedReading   = info;
        return info;
    }

    // Suspend flash LED, activate I2C on GPIO 4 + GPIO 0
    ledcDetachPin(FLASH_LED_PIN);
    Wire.begin(BAT_SDA_PIN, BAT_SCL_PIN);

    // Read INA219 (~1-2ms)
    info.voltage_V  = ina219.getBusVoltage_V();
    info.current_mA = ina219.getCurrent_mA();
    info.power_mW   = ina219.getPower_mW();

    // Release I2C, restore flash LED
    Wire.end();
    ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);

    // Calculate battery state
    float mv = info.voltage_V * 1000.0;
    info.percentage = voltageToPercent(mv);

    if (info.current_mA < -10) {
        info.status = BAT_CHARGING;
    } else if (mv >= 3800) {
        info.status = BAT_FULL;
    } else if (mv >= BATTERY_LOW_MV) {
        info.status = BAT_NORMAL;
    } else if (mv >= BATTERY_CRITICAL_MV) {
        info.status = BAT_LOW;
    } else {
        info.status = BAT_CRITICAL;
    }

    cachedReading = info;
    return info;
}

BatteryInfo batteryGetCached() {
    return cachedReading;
}

const char* batteryStatusStr(BatteryStatus s) {
    switch (s) {
        case BAT_FULL:     return "full";
        case BAT_NORMAL:   return "normal";
        case BAT_LOW:      return "low";
        case BAT_CRITICAL: return "critical";
        case BAT_CHARGING: return "charging";
        default:           return "unknown";
    }
}
