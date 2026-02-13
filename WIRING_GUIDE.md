# ESP32-CAM Wiring Guide

## Overview

This guide shows how to connect your ESP32-CAM for programming and operation.

## Components Needed

1. **ESP32-CAM Module** (AI-Thinker)
2. **FTDI Programmer** or USB-to-TTL adapter (with 5V output)
3. **Jumper Wires** (Female-to-Female recommended)
4. **MicroSD Card** (4-32GB, FAT32 formatted, Class 10)
5. Optional: **USB Power Adapter** (5V, 1A minimum)

---

## Wiring for Programming (Upload Code)

Connect the ESP32-CAM to your FTDI programmer as follows:

```
ESP32-CAM          FTDI Programmer
=========          ===============
GND     ────────── GND
5V      ────────── VCC (5V)
U0R     ────────── TX
U0T     ────────── RX
IO0     ────────── GND (only for programming)
```

### Important Notes:
- **IO0 to GND**: This connection is ONLY needed during code upload
- **RX/TX are CROSSED**: ESP32 U0R connects to FTDI TX, and vice versa
- **Use 5V**: The ESP32-CAM needs 5V, not 3.3V (especially with camera)
- **Remove IO0-GND**: After upload, disconnect IO0 from GND before pressing RESET

### Detailed Connections:

| ESP32-CAM Pin | Description | FTDI Pin | Notes |
|---------------|-------------|----------|-------|
| GND | Ground | GND | Common ground |
| 5V | Power | VCC (5V) | Must be 5V, not 3.3V |
| U0R (RX) | Serial Receive | TX | Crossed connection |
| U0T (TX) | Serial Transmit | RX | Crossed connection |
| IO0 | Boot Mode | GND | Only during upload |

---

## Programming Steps

### 1. Connect for Programming
1. Connect all wires as shown above
2. **Important**: Connect IO0 to GND
3. Connect FTDI programmer to computer USB port
4. Power LED on ESP32-CAM should light up

### 2. Upload Code
1. Open Arduino IDE
2. Select Board: **Tools → Board → AI Thinker ESP32-CAM**
3. Select Port: **Tools → Port → (your FTDI port)**
4. Click Upload button
5. Wait for "Connecting..." message
6. If it doesn't connect, press RESET button on ESP32-CAM
7. Wait for upload to complete (may take 1-2 minutes)

### 3. Switch to Run Mode
1. **Disconnect IO0 from GND** (very important!)
2. Press RESET button on ESP32-CAM
3. Open Serial Monitor (115200 baud)
4. You should see startup messages and WiFi connection

---

## Wiring for Operation (After Programming)

Once code is uploaded, you can remove the IO0-GND connection:

```
ESP32-CAM          FTDI Programmer or Power
=========          ========================
GND     ────────── GND
5V      ────────── VCC (5V)
U0R     ────────── TX (optional, for serial monitor)
U0T     ────────── RX (optional, for serial monitor)
```

### Standalone Operation:
For standalone operation, you only need power:
- Connect 5V to the 5V pin
- Connect GND to GND
- That's it! The ESP32-CAM will boot and run automatically

### Power Options:
1. **FTDI Programmer**: Keep connected for power + serial debugging
2. **USB Power Adapter**: 5V USB adapter with wires to 5V/GND pins
3. **Battery**: 5V battery pack (with proper voltage regulation)

---

## SD Card Installation

1. **Format SD Card**: Format as FAT32 on your computer
2. **Insert Card**: Push the microSD card into the slot on the back of ESP32-CAM
   - **Card orientation**: Contacts facing the board
   - Push until it clicks
3. **Power On**: The ESP32-CAM will detect and initialize the card on boot

### SD Card Specs:
- **Format**: FAT32 (exFAT not supported)
- **Capacity**: 4GB to 32GB (FAT32 limit)
- **Speed**: Class 10 recommended
- **Brands**: Use reliable brands (SanDisk, Samsung, Kingston)

---

## ESP32-CAM Pinout Reference

```
                   ┌─────────────────────┐
                   │                     │
    ANTENNA    ====│  ESP32-CAM Module   │
                   │                     │
       GND ────  1 │ o                 o │ 36  GPIO32 (LED)
      3.3V ────  2 │ o                 o │ 35  GND
    GPIO4 ────  3 │ o   AI-THINKER    o │ 34  5V
       U0R ────  4 │ o                 o │ 33  GPIO33
       U0T ────  5 │ o                 o │ 32  GPIO1 (TX)
       GND ────  6 │ o    [CAMERA]     o │ 31  GPIO3 (RX)
        5V ────  7 │ o                 o │ 30  GND
                   │                     │
                   │   [SD CARD SLOT]    │
                   │     (on back)       │
                   └─────────────────────┘
```

### Important Pins:
- **GPIO0 (IO0)**: Boot mode selection (not on pinout, but available)
- **GPIO4**: Flash LED (high-power, be careful)
- **GPIO33**: Built-in red LED indicator
- **RESET**: Reset button on the board
- **U0R/U0T**: Serial communication pins

---

## Troubleshooting Connection Issues

### Problem: Upload Fails / Won't Connect

**Solutions:**
1. Check IO0 is connected to GND during upload
2. Try pressing RESET button when you see "Connecting..."
3. Verify RX/TX are crossed correctly
4. Try lower upload speed: **Tools → Upload Speed → 115200**
5. Check your FTDI provides enough current (some can't power ESP32-CAM)

### Problem: ESP32-CAM Won't Boot After Upload

**Solutions:**
1. Disconnect IO0 from GND
2. Press RESET button
3. Check 5V power supply has enough current (500mA minimum)

### Problem: Serial Monitor Shows Garbage Text

**Solutions:**
1. Set baud rate to 115200
2. Press RESET button
3. Check connections are secure

### Problem: Camera or SD Card Not Working

**Solutions:**
1. Camera: Check board selection is "AI Thinker ESP32-CAM"
2. SD Card: Ensure formatted as FAT32
3. SD Card: Try a different card (some are incompatible)
4. Power: Ensure adequate 5V supply (camera needs power)

---

## Safety Warnings

⚠️ **Important Safety Information:**

1. **Never use 3.3V**: The ESP32-CAM with camera requires 5V
2. **Check Polarity**: Wrong polarity can damage the board
3. **Current Requirements**: Provide at least 500mA at 5V
4. **Hot Surface**: The ESP32 chip can get warm during operation
5. **Static Electricity**: Handle the board by its edges to avoid ESD damage
6. **Flash LED**: GPIO4 controls a high-power flash LED - don't stare at it directly

---

## Next Steps

After successful wiring and upload:

1. **Check Serial Monitor**: Verify ESP32-CAM boots and connects to WiFi
2. **Note IP Address**: The IP will be shown in Serial Monitor
3. **Test Web Interface**: Open the IP in your browser
4. **Test API**: Run the test scripts in `esp32_hopfog/test_api.py`
5. **Check SD Card**: Dashboard will show if SD card is detected

For more detailed information, see [ESP32_README.md](ESP32_README.md)

---

## Visual Reference

### FTDI Programmer Pin Labels:
```
┌─────┐
│ GND │  Black wire
│ CTS │
│ VCC │  Red wire (5V)
│ TXD │  Green wire
│ RXD │  White wire
│ DTR │
└─────┘
```

### ESP32-CAM Important Locations:
```
     ┌─────────────────┐
     │  [CAMERA LENS]  │
     │                 │
     │  [ESP32 CHIP]   │
     │                 │
  →  │  RESET BUTTON   │
     │                 │
     │  [ANTENNA]      │
     └─────────────────┘
     │  SD CARD SLOT   │  ← On the back
     └─────────────────┘
```

---

## Additional Resources

- [ESP32-CAM Datasheet](https://components101.com/development-boards/esp32-cam-pinout-features-datasheet)
- [Arduino ESP32 Core](https://github.com/espressif/arduino-esp32)
- [ESP32 Forum](https://esp32.com/)
