# XBee S2C Testing Guide

How to test XBee S2C (ZigBee) communication between the ESP32 admin and HopFog-Node devices.

---

## XBee Mode: AT/Transparent (AP=0)

HopFog uses **AT/transparent mode** (AP=0) with **newline-delimited JSON** over serial. Both the admin (HopFog-Web) and nodes (HopFog-Node) use the same text-based protocol:

- **Send:** `Serial.println(json)` — writes JSON text + newline
- **Receive:** Buffer bytes until `\n`, then parse as JSON
- **No binary API frames** — pure text mode

This is simple, debuggable (human-readable in any serial monitor), and compatible across all XBee S2C variants (regular and Pro).

---

## Mixing XBee S2C Pro and Regular S2C

**Yes — XBee S2C Pro and regular XBee S2C are fully compatible.** You can mix them freely in the same ZigBee network. They use:

- Same ZigBee protocol and firmware (ZB function set)
- Same XCTU configuration parameters (PAN ID, CE, BD, DH, DL)
- Same serial interface (UART, 3.3V)

The **only difference** is radio transmit power and range:

| Module | Transmit Power | Indoor Range | Outdoor Range |
|--------|---------------|-------------|---------------|
| **XBee S2C** (regular) | 2 mW (+3 dBm) | ~40 m | ~120 m |
| **XBee S2C Pro** | 63 mW (+18 dBm) | ~90 m | ~3.2 km |

### Recommended Topology

Use the **S2C Pro as Coordinator** (admin/ESP32 side) for maximum range, and **regular S2C as Routers** on nodes:

```
   ┌──────────────────┐          ┌──────────────────┐
   │  Admin (ESP32)   │          │  Node A          │
   │  XBee S2C Pro    │◄──3km──►│  XBee S2C        │
   │  CE=Coordinator  │          │  CE=Router       │
   │  AP=0 (AT mode)  │          │  AP=0 (AT mode)  │
   └──────────────────┘          └──────────────────┘
           ▲                            ▲
           │ ~3km range                 │ ~120m range
           ▼                            ▼
   ┌──────────────────┐          ┌──────────────────┐
   │  Node B          │          │  Node C          │
   │  XBee S2C        │          │  XBee S2C        │
   │  CE=Router       │          │  CE=Router       │
   └──────────────────┘          └──────────────────┘
```

> **Multi-hop mesh:** All XBees configured as Routers (CE=0) will automatically relay frames to extend range beyond direct line-of-sight.

---

## What You Need

| Item | Purpose |
|------|---------|
| **ESP32-CAM** (or any ESP32) | Runs HopFog firmware with XBee attached via UART |
| **XBee S2C module #1** (Pro or regular) | Connected to the ESP32 admin (wired to UART2) |
| **XBee S2C module #2** (Pro or regular) | Connected to your PC via an XBee USB explorer/adapter |
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
| **AP** (API Enable) | `Transparent Mode [0]` | AT/transparent mode — raw text |
| **BD** (Baud Rate) | `9600 [3]` | Must match `XBEE_BAUD` in config.h |
| **DH** (Dest. Address High) | `0` | Broadcast high byte |
| **DL** (Dest. Address Low) | `FFFF` | Broadcast to all devices |

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
| **AP** (API Enable) | `Transparent Mode [0]` | AT/transparent mode — raw text |
| **BD** (Baud Rate) | `9600 [3]` | Matches config.h |
| **DH** (Dest. Address High) | `0` | Broadcast high byte |
| **DL** (Dest. Address Low) | `FFFF` | Broadcast to all devices |

3. Click **"Write"** to save
4. Unplug XBee #1 from the USB explorer and wire it to the ESP32

> **Node XBees:** Configure the same way (AP=0, DH=0, DL=FFFF, same PAN ID, CE=0).

---

## Step 4: Wire XBee #1 to ESP32

Both ESP32-CAM and generic ESP32 use **UART2 on GPIO 13/12**.
This keeps UART0 (Serial Monitor) free for debug output.

```
ESP32 GPIO 13 (TX) ──→ XBee DIN  (pin 3)
ESP32 GPIO 12 (RX) ←── XBee DOUT (pin 2)
ESP32 3.3V         ──→ XBee VCC  (pin 1)
ESP32 GND          ──→ XBee GND  (pin 10)
```

> **Important:** XBee S2C runs on 3.3V. Do NOT connect to 5V.

### ESP32-CAM Note: GPIO 12 Boot Strapping

GPIO 12 is a boot-strapping pin that selects VDD_SDIO voltage. If the
XBee holds GPIO 12 HIGH during power-on, the ESP32 may fail to boot.
If you experience boot problems:

1. **Disconnect** XBee DOUT (GPIO 12) during power-on, reconnect after boot
2. **Or** burn the VDD_SDIO efuse to permanently force 3.3V (one-time fix):
   ```cmd
   python -m espefuse --port COM3 set_flash_voltage 3.3V
   ```

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
[XBee] UART2 started (AT mode) — TX=GPIO13  RX=GPIO12  baud=9600
```

---

## Step 6: Test with XCTU Terminal

Since both XBees are in AT/transparent mode, you can use XCTU's **serial terminal** (not the API frame console):

1. In XCTU, select XBee module #2 (the PC-side coordinator)
2. Click the **"Console"** tab (terminal icon)
3. Click **"Open"** to start the serial connection
4. Switch to **text mode** (not hex mode)

Any text you type and send will be broadcast to all devices in the PAN.

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
6. The UI will show the result

### Option B: Via the API directly

```cmd
curl -X POST http://192.168.4.1/api/xbee/test ^
     -H "Cookie: session=YOUR_TOKEN" ^
     -d "message=Hello from HopFog!"
```

---

## Step 8: Verify in XCTU Terminal

In the XCTU terminal, you should see the JSON text appear as a single line:

```
{"cmd":"BROADCAST_MSG","params":{"from":"admin","to":"all","message":"Hello from HopFog!","subject":"Test","msg_type":"test"}}
```

This is human-readable JSON — no binary framing needed.

---

## Step 9: Send a Message FROM XCTU TO the ESP32

1. In the XCTU terminal (text mode), type a JSON command and press Enter:

```json
{"cmd":"REGISTER","node_id":"test-node","ts":12345,"params":{"device_name":"XCTU Test","ip_address":"0.0.0.0"}}
```

2. Press Enter to send (the newline is the message delimiter)

3. On the ESP32 serial monitor, you should see:

```
[NODE] CMD=REGISTER from test-node
[NODE] Registered test-node ()
```

4. The admin will reply with a `REGISTER_ACK` which you'll see in XCTU:

```
{"cmd":"REGISTER_ACK","node_id":"test-node"}
```

---

## Troubleshooting

### Nothing appears in XCTU Terminal

| Check | Fix |
|-------|-----|
| PAN ID mismatch | Both XBees must have the same **ID** (e.g., `1234`) |
| AP mode mismatch | Both must be set to **AP = 0** (Transparent Mode) |
| DH/DL not set | Both must have **DH=0, DL=FFFF** for broadcast |
| Baud rate mismatch | XCTU serial port baud must match XBee's **BD** setting (9600) |
| Not associated | In XCTU, check **AI** (Association Indication) — should be `0x00` (associated) |
| Wrong wiring | Verify DIN/DOUT connections (TX→DIN, RX←DOUT, NOT crossed twice) |

### ESP32 serial shows no XBee activity

- Verify `xbeeInit()` is called in `setup()` (check `src/main.cpp`)
- Check wiring: GPIO 13 → XBee DIN, GPIO 12 → XBee DOUT
- Verify XBee module is getting 3.3V power (LED on the XBee should blink)

### ESP32-CAM: Boot failure with XBee connected

GPIO 12 is a boot-strapping pin. If XBee DOUT holds it HIGH during
power-on, the ESP32 may fail to boot. Disconnect XBee from GPIO 12
during power-on, or burn the VDD_SDIO efuse (see Step 4).

---

## Message Format Reference

All HopFog XBee messages use **newline-delimited JSON**:

| Command | Direction | Example |
|---------|-----------|---------|
| `REGISTER` | Node → Admin | `{"cmd":"REGISTER","node_id":"node-01","params":{"device_name":"Node 1","ip_address":"192.168.4.1"}}` |
| `REGISTER_ACK` | Admin → Node | `{"cmd":"REGISTER_ACK","node_id":"node-01"}` |
| `HEARTBEAT` | Node → Admin | `{"cmd":"HEARTBEAT","node_id":"node-01","params":{"uptime":300,"free_heap":45000}}` |
| `PONG` | Admin → Node | `{"cmd":"PONG","node_id":"node-01"}` |
| `SYNC_REQUEST` | Node → Admin | `{"cmd":"SYNC_REQUEST","node_id":"node-01"}` |
| `SYNC_DATA` | Admin → Node | `{"cmd":"SYNC_DATA","node_id":"node-01","users":[...],"announcements":[...]}` |
| `BROADCAST_MSG` | Admin → Node | `{"cmd":"BROADCAST_MSG","params":{"from":"admin","to":"all","message":"Stay indoors"}}` |

---

## Useful XCTU Features

- **Network Discovery:** Click the network icon to see all XBee modules in the PAN
- **Range Test:** Built-in tool to test signal strength and packet loss
- **Firmware Update:** Keep your XBee S2C firmware up to date via XCTU
- **Terminal:** Type and receive text directly in AT/transparent mode
