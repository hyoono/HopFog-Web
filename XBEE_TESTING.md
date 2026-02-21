# XBee S2C Testing Guide

How to test XBee S2C (ZigBee) communication between the ESP32 and a second XBee module using Digi's XCTU software.

---

## What You Need

| Item | Purpose |
|------|---------|
| **ESP32-CAM** (or any ESP32) | Runs HopFog firmware with XBee attached via UART |
| **XBee S2C module #1** | Connected to the ESP32 (wired to UART2) |
| **XBee S2C module #2** | Connected to your PC via an XBee USB explorer/adapter |
| **XBee USB Explorer** | Sparkfun XBee Explorer, Digi XBIB-U-DEV, or similar USB adapter |
| **XCTU** | Digi's free configuration & testing software |
| **Micro-USB cable** | To connect the XBee USB explorer to your PC |

---

## Step 1: Install XCTU

1. Download XCTU from [digi.com/xctu](https://www.digi.com/products/embedded-systems/digi-xbee/digi-xbee-tools/xctu)
2. Install and open it on your Windows PC

---

## Step 2: Configure XBee Module #2 (PC Side)

Plug XBee #2 into the USB explorer, connect to your PC, then in XCTU:

1. Click **"Discover radio modules"** (magnifying glass icon)
2. Select the COM port for your USB explorer, click **Next → Finish**
3. Once discovered, click the module to open its settings
4. Set these parameters:

| Parameter | Setting | Description |
|-----------|---------|-------------|
| **ID** (PAN ID) | `1234` | Must match on both XBees |
| **CE** (Coordinator Enable) | `Coordinator [1]` | This XBee is the coordinator |
| **AP** (API Enable) | `API Mode Without Escapes [1]` | API mode 1 (matches ESP32 code) |
| **BD** (Baud Rate) | `9600 [3]` | Must match `XBEE_BAUD` in config.h |

5. Click **"Write"** (pencil icon) to save settings to the module

---

## Step 3: Configure XBee Module #1 (ESP32 Side)

Remove XBee #1 from the ESP32, plug it into the USB explorer temporarily:

1. In XCTU, discover the module
2. Set these parameters:

| Parameter | Setting | Description |
|-----------|---------|-------------|
| **ID** (PAN ID) | `1234` | Same PAN ID as module #2 |
| **CE** (Coordinator Enable) | `Join Network [0]` | This one is a router/end device |
| **AP** (API Enable) | `API Mode Without Escapes [1]` | API mode 1 |
| **BD** (Baud Rate) | `9600 [3]` | Matches config.h |

3. Click **"Write"** to save
4. Unplug XBee #1 from the USB explorer and wire it to the ESP32

---

## Step 4: Wire XBee #1 to ESP32

Default pin configuration (from `include/config.h`):

```
ESP32 GPIO 13 (TX) ──→ XBee DIN  (pin 3)
ESP32 GPIO 12 (RX) ←── XBee DOUT (pin 2)
ESP32 3.3V         ──→ XBee VCC  (pin 1)
ESP32 GND          ──→ XBee GND  (pin 10)
```

> **Important:** XBee S2C runs on 3.3V. Do NOT connect to 5V.

### XBee S2C Pinout (top view, antenna up)

```
         ┌─────────┐
    VCC  │ 1    20 │  NC
    DOUT │ 2    19 │  NC
    DIN  │ 3    18 │  NC
    NC   │ 4    17 │  NC
   RESET │ 5    16 │  NC
    NC   │ 6    15 │  NC
    NC   │ 7    14 │  NC
    NC   │ 8    13 │  NC
    NC   │ 9    12 │  NC
    GND  │ 10   11 │  NC
         └─────────┘
```

---

## Step 5: Flash the ESP32 and Verify Boot

```cmd
pio run -e esp32cam -t upload
pio device monitor -b 115200
```

You should see in the serial output:

```
[XBee] UART2 started — TX=13  RX=12  baud=9600
```

---

## Step 6: Open XCTU Console on the PC

1. In XCTU, select XBee module #2 (the PC-side coordinator)
2. Click the **"Console"** tab (terminal icon) in the top toolbar
3. Click **"Open"** (the plug icon) to open the serial connection
4. You should see the connection status turn green

The console is now listening for incoming XBee frames.

---

## Step 7: Send a Test Message from HopFog

### Option A: From the Web UI (recommended)

1. Connect your phone/laptop to the **"HopFog-Network"** WiFi
2. Open **http://hopfog.com** in your browser
3. Log in with your admin account
4. Navigate to **Testing** (in the Admin sidebar)
5. In the **XBee S2C Communication Test** panel:
   - Type a custom message (or use the default)
   - Click **"Send Test Message"**
6. The UI will show the frame ID and payload that was sent

### Option B: Via the API directly

```cmd
curl -X POST http://192.168.4.1/api/xbee/test ^
     -H "Cookie: session=YOUR_TOKEN" ^
     -d "message=Hello from HopFog!"
```

### Option C: Mark a broadcast as sent

1. Go to **Broadcasts** → create a broadcast → click **Mark Sent (Simulation)**
2. This triggers an XBee broadcast with the payload: `TYPE|SUBJECT|BODY`

---

## Step 8: Verify in XCTU Console

In the XCTU Console tab, you should see an incoming **Receive Packet (0x90)** frame:

```
7E              ← Start delimiter
00 XX           ← Length (MSB, LSB)
90              ← Frame type: ZigBee Receive Packet
XX XX XX XX XX XX XX XX  ← 64-bit source address (XBee #1)
XX XX           ← 16-bit source address
XX              ← Receive options
48 4F 50 46 4F 47 5F 54 45 53 54 7C ...  ← RF data (your message)
XX              ← Checksum
```

The RF data portion is your message in ASCII. For example, `HOPFOG_TEST|Hello from HopFog!` appears as:

```
48 4F 50 46 4F 47 5F 54 45 53 54 7C 48 65 6C 6C 6F 20 66 72 6F 6D 20 48 6F 70 46 6F 67 21
```

> **Tip:** In XCTU Console, click the **"Show frames"** toggle to see parsed frame details instead of raw hex.

---

## Step 9: Send a Message FROM XCTU TO the ESP32

1. In XCTU Console, click **"Send frame"** (the "+" icon or Ctrl+Shift+F)
2. Build a **Transmit Request (0x10)** frame:

```
7E 00 1A 10 01 00 00 00 00 00 00 FF FF FF FE 00 00 48 45 4C 4C 4F 7C 48 69 21 XX
```

Breaking it down:
| Bytes | Meaning |
|-------|---------|
| `7E` | Start delimiter |
| `00 1A` | Length = 26 bytes |
| `10` | Frame type: Transmit Request |
| `01` | Frame ID (for TX status ACK) |
| `00 00 00 00 00 00 FF FF` | 64-bit broadcast address |
| `FF FE` | 16-bit address (unknown) |
| `00` | Broadcast radius (max hops) |
| `00` | Options (default) |
| `48 45 4C 4C 4F 7C 48 69 21` | RF data: "HELLO|Hi!" |
| `XX` | Checksum (auto-calculated by XCTU) |

3. Click **Send**

4. On the ESP32 serial monitor, you should see:

```
[XBee] RX from 0013A200XXXXXXXX (9 bytes): HELLO|Hi!
```

---

## Troubleshooting

### Nothing appears in XCTU Console

| Check | Fix |
|-------|-----|
| PAN ID mismatch | Both XBees must have the same **ID** (e.g., `1234`) |
| API mode mismatch | Both must be set to **AP = 1** (API Mode Without Escapes) |
| Baud rate mismatch | XCTU serial port baud must match XBee's **BD** setting (9600) |
| Not associated | In XCTU, check **AI** (Association Indication) — should be `0x00` (associated) |
| Wrong wiring | Verify DIN/DOUT connections (TX→DIN, RX←DOUT, NOT crossed twice) |

### ESP32 serial shows `[XBee] TX status ... FAIL`

- XBee #1 may not have joined the PAN yet. Wait 10-15 seconds after power-on.
- Check that XBee #1's **CE** is set to `0` (not coordinator) if XBee #2 is the coordinator.
- Verify the PAN IDs match.

### XCTU says "Could not find any radio module"

- Make sure the correct COM port is selected (check Windows Device Manager)
- Try a different USB port
- Update the FTDI/USB driver for your XBee explorer board

### ESP32 serial shows nothing about XBee

- Verify `xbeeInit()` is called in `setup()` (check `src/main.cpp`)
- Check wiring: GPIO 13 → DIN, GPIO 12 → DOUT
- Verify XBee module is getting 3.3V power (LED on the XBee should blink)

---

## Message Format Reference

All HopFog XBee messages use **pipe-delimited** (`|`) format:

| Source | Format | Example |
|--------|--------|---------|
| Test message | `HOPFOG_TEST\|{message}` | `HOPFOG_TEST\|Hello from HopFog!` |
| Broadcast mark-sent | `{msg_type}\|{subject}\|{body}` | `announcement\|Weather Alert\|Stay indoors` |
| SOS escalation | `{escalate_to}\|{subject}\|{body}` | `broadcast\|SOS from Unit 4B\|Medical emergency` |

---

## Useful XCTU Features

- **Network Discovery:** Click the network icon to see all XBee modules in the PAN
- **Range Test:** Built-in tool to test signal strength and packet loss
- **Firmware Update:** Keep your XBee S2C firmware up to date via XCTU
- **Frame Generator:** Build and send custom API frames without manual hex calculation
