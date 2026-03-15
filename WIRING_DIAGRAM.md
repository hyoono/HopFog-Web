# HopFog System — Complete Wiring Diagram

## System Overview

```
                        ┌─────────────────────────────────────┐
                        │          POWER SOURCE               │
                        │   (5V USB or 3.7V 18650 LiPo)      │
                        └──────────┬──────────────────────────┘
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
          ┌─────────────┐  ┌────────────┐  ┌──────────┐
          │  ESP32-CAM  │  │  XBee S2C  │  │  INA219  │
          │  (Admin or  │  │  Module    │  │  Battery │
          │   Node)     │  │            │  │  Monitor │
          └─────────────┘  └────────────┘  └──────────┘
```

---

## Full Circuit Wiring

```
                                    ESP32-CAM (AI-Thinker)
                              ┌─────────────────────────────┐
                              │          TOP VIEW            │
                              │    ┌───────────────────┐     │
                              │    │   Camera Connector │     │
                              │    │   (NOT USED)       │     │
                              │    └───────────────────┘     │
                              │                              │
                   3.3V ──────┤ 3V3                  VCC ├───── 5V IN
                              │                              │
                   GND ───┬───┤ GND                  GND ├───┬── GND
                          │   │                              │  │
   XBee DOUT (pin 2) ─────┤───┤ GPIO 3 (U0RXD)   GPIO 16├───┤  │ (PSRAM CS
                          │   │                              │  │  DO NOT USE)
   XBee DIN  (pin 3) ─────┤───┤ GPIO 1 (U0TXD)   GPIO  0├─┐ │  │
                          │   │                            │ │  │
                          │   │                            │ │  │
                          │   │  GPIO 4 (Flash LED) ├──┐   │ │  │
                          │   │                        │   │ │  │
   SD Card Slot ──────────┤───┤ GPIO 13 (SD CS)        │   │ │  │
    (built-in)            │   │ GPIO 14 (SD CLK)       │   │ │  │
                          │   │ GPIO  2 (SD MISO)      │   │ │  │
                          │   │ GPIO 15 (SD MOSI)      │   │ │  │
                          │   │                        │   │ │  │
                          │   │  GPIO 33 (Red LED) ├───┤   │ │  │
                          │   │   (built-in, boot)     │   │ │  │
                          │   │                        │   │ │  │
                          │   │  GPIO 12 ├─────────────┤   │ │  │
                          │   │  (SD DAT2, do not use) │   │ │  │
                          │   └────────────────────────┘   │ │  │
                          │                                │ │  │
                          │   ┌─── INA219 SDA ◄────────────┘ │  │
                          │   │    (time-shared w/ LED)       │  │
                          │   │                               │  │
                          │   │   INA219 SCL ◄────────────────┘  │
                          │   │                                  │
                          │   │                                  │
                          └───┼──────────────────────────────────┘
                              │
                              ▼
                    (Common Ground Bus)
```

---

## Detailed Connection Table

### ESP32-CAM ↔ XBee S2C Pro

```
    ESP32-CAM                          XBee S2C Module
    ─────────                          ──────────────
                                   ┌──────────────────┐
                                   │     XBee S2C     │
                                   │    (TOP VIEW)    │
                                   │                  │
    3.3V ──────────────────────────┤ 1  VCC     VCC 20├
    GPIO 3 (U0RXD) ◄──────────────┤ 2  DOUT         │
    GPIO 1 (U0TXD) ───────────────┤ 3  DIN          │
                                   │ 4  (NC)         │
                                   │ 5  RESET        │
                                   │ ...             │
    GND ───────────────────────────┤ 10 GND     GND  │
                                   │                  │
                                   └──────────────────┘

    Baud Rate: 9600 (BD=3)
    API Mode:  1 (AP=1, binary framed)
    Role:      CE=1 (Coordinator/Admin) or CE=0 (Router/Node)
```

### ESP32-CAM ↔ INA219 Battery Monitor

```
    ESP32-CAM                          INA219 Module
    ─────────                          ──────────────
                                   ┌──────────────────┐
                                   │     INA219       │
                                   │                  │
    GPIO 4 ────────────────────────┤ SDA              │
    (time-shared with flash LED)   │                  │
                                   │                  │
    GPIO 0 ────────────────────────┤ SCL              │
    (free after boot)              │                  │
                                   │                  │
    3.3V ──────────────────────────┤ VCC              │
                                   │                  │
    GND ───────────────────────────┤ GND              │
                                   │                  │
                                   │  VIN+ ──┐        │
                                   │         │ Shunt  │
                                   │  VIN- ──┘        │
                                   └──────────────────┘

    VIN+  →  Battery positive terminal
    VIN-  →  ESP32-CAM 5V input (or regulator input)
    (INA219 shunt resistor measures current inline)
```

### INA219 Battery Measurement Circuit

```
                    ┌──────── VIN+ ────────┐
                    │                      │
    ┌───────────┐   │   ┌──────────────┐   │   ┌─────────────┐
    │  Battery  ├───┘   │   INA219     │   └───┤  ESP32-CAM  │
    │  3.7V     │       │   Module     │       │  5V Input   │
    │  18650    ├───┐   │              │   ┌───┤  (via boost  │
    │  LiPo     │   │   │  Measures:   │   │   │   converter │
    └───────────┘   │   │  • Voltage   │   │   │   or direct)│
                    │   │  • Current   │   │   └─────────────┘
                    └───┤  • Power     ├───┘
                        │              │
                    ┌───┤  VIN-        │
                    │   └──────────────┘
                    │
                    └──── To ESP32 VCC rail
```

---

## Pin Assignment Summary

```
    ╔══════════╦═══════════════════════════╦═══════════════════════════════╗
    ║  GPIO    ║  Function                 ║  Notes                       ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║    0     ║  INA219 SCL (I2C)         ║  Strapping pin (HIGH=boot)   ║
    ║          ║                           ║  I2C pull-up keeps HIGH ✓    ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║    1     ║  XBee TX (UART0 TXD)      ║  DO NOT use for anything     ║
    ║          ║                           ║  else while XBee connected   ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║    2     ║  SD Card MISO (HSPI)      ║  Built-in SD card slot       ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║    3     ║  XBee RX (UART0 RXD)      ║  DO NOT use for anything     ║
    ║          ║                           ║  else while XBee connected   ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║    4     ║  Flash LED (PWM status)   ║  TIME-SHARED: LED 99.99%     ║
    ║          ║  + INA219 SDA (I2C)       ║  I2C ~2ms every 5 seconds    ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   12     ║  SD Card DAT2             ║  ⚠ VDD_SDIO strapping pin   ║
    ║          ║                           ║  MUST be LOW at boot!        ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   13     ║  SD Card CS (HSPI)        ║  Built-in SD card slot       ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   14     ║  SD Card CLK (HSPI)       ║  Built-in SD card slot       ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   15     ║  SD Card MOSI (HSPI)      ║  Built-in SD card slot       ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   16     ║  PSRAM CS                 ║  ⚠ NEVER use as output!     ║
    ╠══════════╬═══════════════════════════╬═══════════════════════════════╣
    ║   33     ║  Built-in Red LED         ║  Active LOW, boot indicator  ║
    ║          ║  (not on header)          ║  only (3 blinks at startup)  ║
    ╚══════════╩═══════════════════════════╩═══════════════════════════════╝
```

---

## Physical Wiring — Step by Step

### Step 1: XBee S2C to ESP32-CAM (3 wires)

```
    Wire Color    From (ESP32-CAM)     To (XBee S2C)
    ──────────    ─────────────────    ──────────────
    RED           3.3V pin             Pin 1  (VCC)
    BLACK         GND pin              Pin 10 (GND)
    GREEN         GPIO 3 (RX)          Pin 2  (DOUT)
    YELLOW        GPIO 1 (TX)          Pin 3  (DIN)
```

### Step 2: INA219 to ESP32-CAM (4 wires)

```
    Wire Color    From (ESP32-CAM)     To (INA219)
    ──────────    ─────────────────    ──────────
    RED           3.3V pin             VCC
    BLACK         GND pin              GND
    BLUE          GPIO 4               SDA
    WHITE         GPIO 0               SCL
```

### Step 3: INA219 to Battery + Load (2 wires, inline)

```
    Wire Color    From                 To
    ──────────    ─────────────────    ──────────────
    RED           Battery (+)          INA219 VIN+
    RED           INA219 VIN-          ESP32-CAM 5V
                                       (via boost converter
                                        if using 3.7V LiPo)
```

### Step 4: SD Card (built-in, no wiring needed)

```
    Just insert a FAT32-formatted MicroSD card into the
    ESP32-CAM's built-in card slot. No external wiring.
```

---

## Complete System Schematic (Single Node)

```
    ┌─────────────┐         ┌─────────────────────────────┐
    │   Battery   │         │        ESP32-CAM             │
    │   3.7V      │         │       (AI-Thinker)           │
    │   18650     │         │                              │
    │   LiPo     (+)───┐   │  ┌──────────┐  ┌─────────┐  │
    │             │     │   │  │ Flash LED│  │ Red LED │  │
    │            (-)──┐ │   │  │ (GPIO 4) │  │(GPIO 33)│  │
    └─────────────┘   │ │   │  │ Status   │  │ Boot    │  │
                      │ │   │  └──────────┘  └─────────┘  │
    ┌─────────────┐   │ │   │                              │
    │   INA219    │   │ │   │  ┌──────────────────────┐   │
    │   Battery   │   │ │   │  │  Built-in SD Slot    │   │
    │   Monitor   │   │ │   │  │  GPIO 13 (CS)        │   │
    │             │   │ │   │  │  GPIO 14 (CLK)       │   │
    │  VIN+ ◄─────┼───┼─┘   │  │  GPIO  2 (MISO)     │   │
    │  VIN- ──────┼───┼─────┤  │  GPIO 15 (MOSI)     │   │
    │             │   │     │  └──────────────────────┘   │
    │  SDA ───────┼───┼─────┤ GPIO 4 (time-shared)        │
    │  SCL ───────┼───┼─────┤ GPIO 0                      │
    │  VCC ───────┼───┼─────┤ 3.3V                        │
    │  GND ───────┼───┼──┬──┤ GND                         │
    └─────────────┘   │  │  │                              │
                      │  │  │         ┌────────────┐       │
    ┌─────────────┐   │  │  │         │ WiFi AP    │       │
    │  XBee S2C   │   │  │  │         │ 192.168.4.1│       │
    │  Module     │   │  │  │         │ (built-in) │       │
    │             │   │  │  │         └────────────┘       │
    │  Pin 1 VCC──┼───┼──┼──┤ 3.3V                        │
    │  Pin 2 DOUT─┼───┼──┼──┤ GPIO 3 (RX) ◄── receive     │
    │  Pin 3 DIN──┼───┼──┼──┤ GPIO 1 (TX) ──► transmit    │
    │  Pin 10 GND─┼───┼──┼──┤ GND                         │
    │             │   │  │  │                              │
    │  Antenna    │   │  │  └──────────────────────────────┘
    │    ╱╲       │   │  │
    │   ╱  ╲      │   │  │
    └─────────────┘   │  │
                      │  │
                      └──┘
                    Common GND
```

---

## Two-Node System (Admin + Node)

```
                         ZigBee Mesh Network
                         ~~~~~~~~~~~~~~~~~~~

    ┌────────────────────┐              ┌────────────────────┐
    │   ADMIN UNIT       │    XBee      │   NODE UNIT        │
    │                    │   Wireless   │                    │
    │  ESP32-CAM         │  ◄────────►  │  ESP32-CAM         │
    │  + XBee S2C Pro    │   (ZigBee)   │  + XBee S2C        │
    │    (Coordinator)   │   9600 bps   │    (Router)        │
    │  + INA219          │              │  + INA219           │
    │  + MicroSD         │              │  + MicroSD          │
    │                    │              │                    │
    │  WiFi AP:          │              │  WiFi AP:          │
    │  "HopFog-Network"  │              │  "HopFog-Node-01"  │
    │  192.168.4.1       │              │  192.168.4.1       │
    │                    │              │                    │
    │  XBee Settings:    │              │  XBee Settings:    │
    │    CE = 1          │              │    CE = 0          │
    │    AP = 1          │              │    AP = 1          │
    │    BD = 3 (9600)   │              │    BD = 3 (9600)   │
    │    Same PAN ID     │              │    Same PAN ID     │
    └────────────────────┘              └────────────────────┘
           │                                    │
      ┌────┴────┐                          ┌────┴────┐
      │  Mobile │                          │  Mobile │
      │  Phones │                          │  Phones │
      │  (WiFi) │                          │  (WiFi) │
      └─────────┘                          └─────────┘
```

---

## Power Options

### Option A: USB Power (Development / Testing)

```
    USB 5V ──────────────► ESP32-CAM 5V pin
    USB GND ─────────────► ESP32-CAM GND pin

    INA219 VIN+ ─── short to ─── VIN-
    (bypass — no battery measurement)
```

### Option B: Battery Power (Deployment)

```
    18650 LiPo (3.7V)
         │(+)
         ▼
    ┌──────────┐        ┌──────────┐
    │  INA219  │        │  Boost   │
    │  VIN+    │        │ Converter│
    │          ├───────►│  3.7V→5V │
    │  VIN-    │        │          ├───► ESP32-CAM 5V
    └──────────┘        └──────────┘
         │
    Battery (-)──────────────────────────► ESP32-CAM GND
```

### Option C: USB + Battery with TP4056 Charger

```
    USB 5V ──────────► TP4056 IN+
    USB GND ─────────► TP4056 IN-

    TP4056 BAT+ ─────► Battery (+) ──► INA219 VIN+
    TP4056 BAT- ─────► Battery (-)

    INA219 VIN- ─────► Boost Converter IN
    Boost OUT ────────► ESP32-CAM 5V

    Common GND bus connects all grounds
```

---

## Flash LED Status Patterns (Built-in GPIO 4)

```
    Pattern              Meaning                    Visual
    ─────────────────    ──────────────────────     ──────────────
    ████████████████     OFF (completely dark)       Idle/No nodes

    █░░░░░█░░░░░█░░     Slow pulse (2s cycle)       Searching...

    ████████████████     Solid dim glow              Connected ✓

    █░█░█░█░█░█░█░█     Fast blink (10Hz)           CRITICAL battery
                                                     (< 5%)

    ██░██░░░░██░██░░     Double blink                Low battery
                                                     (5-15%)

    ░▒▓█▓▒░░▒▓█▓▒░     Slow breathe (fade)         Charging
```

---

## Important Notes

1. **GPIO 4 Time-Sharing**: The flash LED and INA219 SDA share GPIO 4.
   The firmware pauses the LED PWM for ~2ms every 5 seconds to read
   the battery sensor. This is imperceptible to the human eye.

2. **GPIO 0 Boot Mode**: GPIO 0 is a boot strapping pin. The INA219's
   I2C pull-up resistor keeps it HIGH, which is the normal boot mode.
   This actually *helps* reliable booting.

3. **GPIO 12 Warning**: DO NOT connect anything with a pull-up to
   GPIO 12. It's the VDD_SDIO voltage selector — if HIGH at boot,
   it sets flash to 1.8V instead of 3.3V, causing boot failure.

4. **UART0 = XBee**: The USB serial monitor is NOT available when
   XBee is connected. All debugging is via the web dashboard at
   http://192.168.4.1 (connect to the WiFi AP first).

5. **INA219 Optional**: If no INA219 is connected, the system
   auto-detects and runs normally without battery monitoring.
   The `/api/battery` endpoint returns `{"available": false}`.

6. **MicroSD Required**: Format as FAT32. The system creates
   `/db/` and `/www/` directories on first boot.
