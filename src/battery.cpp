/*
 * battery.cpp — INA219 battery monitoring for HopFog admin/node
 *
 * DISABLED BY DEFAULT on ESP32-CAM: GPIO 21/22 (standard I2C) are not
 * broken out on the AI-Thinker ESP32-CAM header pins.
 *
 * To enable: set -DENABLE_BATTERY=1 in platformio.ini build_flags
 * and wire INA219 to GPIO 21 (SDA) + GPIO 22 (SCL) via the camera
 * connector or a breakout board.
 *
 * When disabled, batteryInit() returns false and batteryRead() returns
 * safe defaults. The dashboard shows "N/A".
 */

#include "battery.h"
#include "config.h"

#if ENABLE_BATTERY
#include <Wire.h>
#include <Adafruit_INA219.h>
static Adafruit_INA219 ina219;
#endif

static bool ina219Available = false;

#if ENABLE_BATTERY
static int voltageToPercent(float mv) {
    if (mv >= BATTERY_FULL_MV) return 100;
    if (mv <= BATTERY_EMPTY_MV) return 0;
    return (int)((mv - BATTERY_EMPTY_MV) * 100.0 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}
#endif

bool batteryInit() {
#if ENABLE_BATTERY
    Wire.begin(21, 22);
    if (!ina219.begin(&Wire)) {
        dbgprintln("[BAT] INA219 not found — battery monitoring disabled");
        ina219Available = false;
        return false;
    }
    ina219.setCalibration_16V_400mA();
    ina219Available = true;
    dbgprintln("[BAT] INA219 initialized — battery monitoring active");
    return true;
#else
    ina219Available = false;
    return false;
#endif
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

#if ENABLE_BATTERY
    info.voltage_V  = ina219.getBusVoltage_V();
    info.current_mA = ina219.getCurrent_mA();
    info.power_mW   = ina219.getPower_mW();

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
#endif

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
