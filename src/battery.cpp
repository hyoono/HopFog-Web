/*
 * battery.cpp — INA219 battery monitoring for HopFog admin/node
 *
 * I2C on default Wire (GPIO 21 SDA, GPIO 22 SCL) — but on ESP32-CAM
 * these aren't broken out. We use GPIO 14 (SDA) + GPIO 15 (SCL)
 * with Wire.begin() override. These must be initialized AFTER
 * SD SPI completes to avoid bus contention.
 *
 * If no INA219 is found, all reads return safe defaults and
 * batteryInit() returns false. The dashboard shows "N/A".
 */

#include "battery.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 ina219;
static bool ina219Available = false;

// ── Map voltage to percentage (linear approximation) ────────────────
static int voltageToPercent(float mv) {
    if (mv >= BATTERY_FULL_MV) return 100;
    if (mv <= BATTERY_EMPTY_MV) return 0;
    return (int)((mv - BATTERY_EMPTY_MV) * 100.0 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

bool batteryInit() {
    // ESP32-CAM: use GPIO 14 (SDA) and GPIO 15 (SCL) for I2C
    // These are the same as SD SPI CLK/MOSI, but after SD init
    // we can reconfigure them for I2C if the INA219 is on a separate bus.
    // Default I2C address: 0x40
    Wire.begin(14, 15);

    if (!ina219.begin(&Wire)) {
        dbgprintln("[BAT] INA219 not found — battery monitoring disabled");
        ina219Available = false;
        return false;
    }

    // Use 16V, 400mA range for LiPo monitoring
    ina219.setCalibration_16V_400mA();
    ina219Available = true;
    dbgprintln("[BAT] INA219 initialized — battery monitoring active");
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
        return info;
    }

    info.voltage_V  = ina219.getBusVoltage_V();
    info.current_mA = ina219.getCurrent_mA();
    info.power_mW   = ina219.getPower_mW();

    float mv = info.voltage_V * 1000.0;
    info.percentage = voltageToPercent(mv);

    // Determine status
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

    return info;
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
