# XBee S2C Testing Guide — 2-Module Setup

How to connect and test XBee S2C (ZigBee) communication between the
ESP32-CAM admin (HopFog-Web) and ONE ESP32-CAM node (HopFog-Node).

**Setup:** 2 XBee S2C modules, 2 ESP32-CAMs, NO USB explorer needed for operation.

---

## Quick Reference

| | Admin ESP32-CAM | Node ESP32-CAM |
|---|---|---|
| **Firmware** | HopFog-Web | HopFog-Node |
| **WiFi AP** | HopFog-Network | HopFog-Node-01 |
| **WiFi Password** | changeme123 | changeme123 |
| **Dashboard** | http://192.168.4.1 | http://192.168.4.1/status |
| **XBee Role** | Coordinator (CE=1) | Router (CE=0) |
| **XBee UART** | UART0 GPIO 1/3 | UART0 GPIO 1/3 |
| **Baud** | 9600 | 9600 |

---

## XBee Mode: API Mode 1 (AP=1)

HopFog uses **API mode 1** (AP=1) with **binary-framed packets**. JSON payloads are wrapped in standard XBee API frames:

- **Send:** Build a `0x10` Transmit Request frame → XBee broadcasts the RF data
- **Receive:** Parse `0x90` Receive Packet frame → extract RF data (JSON payload)
- **Advantages:** Delivery status feedback (`0x8B` TX Status), sender address in received frames, reliable framing with checksums

Both the admin (HopFog-Web) and nodes (HopFog-Node) use the same API mode 1 protocol.

---

## Mixing XBee S2C Pro and Regular S2C

**Yes — XBee S2C Pro and regular XBee S2C are fully compatible.** You can mix them freely in the same ZigBee network. They use:

- Same ZigBee protocol and firmware (ZB function set)
- Same XCTU configuration parameters (PAN ID, CE, BD, AP)
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
   │  AP=1 (API mode) │          │  AP=1 (API mode) │
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
| **XBee S2C module #1** (Pro or regular) | Connected to the ESP32 admin (wired to UART1/2) |
| **XBee S2C module #2** (Pro or regular) | Connected to your PC via an XBee USB explorer/adapter |
| **XBee USB Explorer** | Sparkfun XBee Explorer, Digi XBIB-U-DEV, or similar USB adapter |
| **XCTU** | Digi's free configuration & testing software |
| **Micro-USB cable** | To connect the XBee USB explorer to your PC |

---

## Step 1: Install XCTU

1. Download XCTU from [digi.com/xctu](https://www.digi.com/products/embedded-systems/digi-xbee/digi-xbee-tools/xctu)
2. Install and open it on your Windows PC

---

## Step 2: Configure XBee Module #2 (PC/XCTU Test Side)

Plug XBee #2 into the USB explorer, connect to your PC, then in XCTU:

1. Click **"Discover radio modules"** (magnifying glass icon)
2. Select the COM port for your USB explorer, click **Next → Finish**
3. Once discovered, click the module to open its settings
4. Set these parameters:

| Parameter | Setting | Description |
|-----------|---------|-------------|
| **ID** (PAN ID) | `1234` | Must match on all XBees |
| **CE** (Coordinator Enable) | `Join Network [0]` | Router — simulates a node for testing |
| **JV** (Channel Verification) | `Enabled [1]` | Join the coordinator's network automatically |
| **AP** (API Enable) | `API enabled [1]` | API mode 1 — binary framed packets |
| **BD** (Baud Rate) | `9600 [3]` | Must match `XBEE_BAUD` in config.h |

5. Click **"Write"** (pencil icon) to save settings to the module

> **Note:** In API mode 1, DH/DL are not needed for broadcast — the destination address is specified in each Transmit Request frame.

---

## Step 3: Configure XBee Module #1 (ESP32 Admin Side)

Remove XBee #1 from the ESP32, plug it into the USB explorer temporarily:

1. In XCTU, discover the module
2. Set these parameters:

| Parameter | Setting | Description |
|-----------|---------|-------------|
| **ID** (PAN ID) | `1234` | Same PAN ID as module #2 |
| **CE** (Coordinator Enable) | `Coordinator [1]` | Admin is ALWAYS the coordinator |
| **AP** (API Enable) | `API enabled [1]` | API mode 1 — binary framed packets |
| **BD** (Baud Rate) | `9600 [3]` | Matches config.h |

3. Click **"Write"** to save
4. Unplug XBee #1 from the USB explorer and wire it to the ESP32

> **Node XBees:** Configure the same as Module #2 (AP=1, same PAN ID, CE=0, JV=1).

---

## Step 4: Wire XBee #1 to ESP32

### ESP32-CAM — Uses UART0 (GPIO 1/3)

The ESP32-CAM uses UART0 on its native IOMUX pins for the most reliable XBee connection. **USB Serial Monitor is NOT available** — all debug output is disabled at compile time.

```
ESP32 GPIO 1 (U0TXD) ──→ XBee DIN  (pin 3)
ESP32 GPIO 3 (U0RXD) ←── XBee DOUT (pin 2)
ESP32 3.3V            ──→ XBee VCC  (pin 1)
ESP32 GND             ──→ XBee GND  (pin 10)
```

> **IMPORTANT:** Disconnect the XBee before uploading firmware.
> GPIO 1/3 are shared between USB programming and XBee — having both
> connected simultaneously causes bus contention.
>
> **Flash LED (GPIO 4):** The firmware sets GPIO 4 LOW at boot to disable
> the bright flash LED. Do NOT connect GPIO 4 to the XBee.

### Generic ESP32 — Uses UART2 (GPIO 13/12)

```
ESP32 GPIO 13 (TX) ──→ XBee DIN  (pin 3)
ESP32 GPIO 12 (RX) ←── XBee DOUT (pin 2)
ESP32 3.3V         ──→ XBee VCC  (pin 1)
ESP32 GND          ──→ XBee GND  (pin 10)
```

USB Serial Monitor on UART0 remains available for debug output.

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
[XBee] UART1 started (API mode 1) — TX=GPIO3  RX=GPIO12  baud=9600
```

---

## Step 6: Test with XCTU API Frame Tools

Since both XBees are in API mode 1, use XCTU's **API frames console**:

1. In XCTU, select XBee module #2 (the PC-side coordinator)
2. Click the **"Console"** tab (terminal icon)
3. Click **"Open"** to start the serial connection
4. Switch to the **"Frames Generator"** view (API frames mode)

### Send a test JSON to the ESP32

1. Click **"Add Frame"** (+ icon)
2. Select frame type: **0x10 - Transmit Request**
3. Set 64-bit destination: `000000000000FFFF` (broadcast)
4. Set 16-bit destination: `FFFE`
5. Set RF Data (hex-encode your JSON):
   - Example JSON: `{"cmd":"REGISTER","node_id":"xctu-test","params":{"device_name":"XCTU Test"}}`
   - Paste the ASCII text in the RF Data field
6. Click **"Send"**

### Verify the response

The ESP32 will process the command and reply with a `REGISTER_ACK` frame. In XCTU's frame log, you'll see an incoming `0x90 Receive Packet` frame containing:

```json
{"cmd":"REGISTER_ACK","node_id":"xctu-test"}
```

---

## Step 7: Send a Test Message from HopFog

### Option A: From the Web UI (recommended)

1. Connect your phone/laptop to the **"HopFog-Network"** WiFi
2. Open **http://hopfog.com** in your browser
3. Log in with your admin account
4. Navigate to **Testing** (in the Messaging sidebar)
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

## Step 8: Verify in XCTU

In XCTU's frames log, you should see a `0x90 Receive Packet` frame.
Click on it to see the decoded RF data:

```json
{"cmd":"BROADCAST_MSG","params":{"from":"admin","to":"all","message":"Hello from HopFog!","subject":"Test","msg_type":"test"}}
```

---

## Troubleshooting

### Nothing appears in XCTU

| Check | Fix |
|-------|-----|
| PAN ID mismatch | All XBees must have the same **ID** (e.g., `1234`) |
| AP mode mismatch | All must be set to **AP = 1** (API mode 1) |
| Baud rate mismatch | XCTU serial port baud must match XBee's **BD** setting (9600) |
| Not associated | In XCTU, check **AI** (Association Indication) — should be `0x00` (associated) |
| Wrong wiring | Verify DIN/DOUT connections (TX→DIN, RX←DOUT, NOT crossed twice) |
| XCTU in text mode | Switch to **API frames mode** (not serial terminal text mode) |

### ESP32 serial shows no XBee activity

- Verify `xbeeInit()` is called in `setup()` (check `src/main.cpp`)
- Check wiring: GPIO 13 → XBee DIN, GPIO 12 → XBee DOUT
- Verify XBee module is getting 3.3V power (LED on the XBee should blink)

### ESP32 serial shows "RX frame checksum error"

- XBee module may not be in API mode 1 (check AP=1 in XCTU)
- Baud rate mismatch between ESP32 and XBee module
- Electrical noise on UART lines (try shorter wires)

### ESP32 serial shows "TX status: delivery FAILED"

- No other XBee in range to receive the frame
- Destination XBee not associated with the network (check AI=0x00)

### ESP32-CAM: Boot failure with XBee connected

GPIO 12 is a boot-strapping pin. If XBee DOUT holds it HIGH during
power-on, the ESP32 may fail to boot. Disconnect XBee from GPIO 12
during power-on, or burn the VDD_SDIO efuse (see Step 4).

---

## Message Format Reference

All HopFog XBee messages are JSON payloads inside API mode 1 frames:

| Command | Direction | Payload (inside 0x10/0x90 frame) |
|---------|-----------|---------|
| `REGISTER` | Node → Admin | `{"cmd":"REGISTER","node_id":"node-01","params":{"device_name":"Node 1","ip_address":"192.168.4.1"}}` |
| `REGISTER_ACK` | Admin → Node | `{"cmd":"REGISTER_ACK","node_id":"node-01"}` |
| `HEARTBEAT` | Node → Admin | `{"cmd":"HEARTBEAT","node_id":"node-01","params":{"uptime":300,"free_heap":45000}}` |
| `PONG` | Admin → Node | `{"cmd":"PONG","node_id":"node-01"}` |
| `SYNC_REQUEST` | Node → Admin | `{"cmd":"SYNC_REQUEST","node_id":"node-01"}` |
| `SYNC_DATA` | Admin → Node | `{"cmd":"SYNC_DATA","node_id":"node-01","users":[...],"announcements":[...]}` |
| `BROADCAST_MSG` | Admin → Node | `{"cmd":"BROADCAST_MSG","params":{"from":"admin","to":"all","message":"Stay indoors"}}` |

---

## API Mode 1 Frame Reference

### Transmit Request (0x10) — sent by ESP32/node

```
Byte:  0x7E | LenHi LenLo | 0x10 FrameID Dest64[8] Dest16[2] Radius Options | RFData... | Checksum
       ──── | ──────────── | ─────────────────────────────────────────────── | ───────── | ────────
Start  Delim  Length         Frame Type + Header (14 bytes)                    Payload     0xFF - sum
```

### Receive Packet (0x90) — received by ESP32/node

```
Byte:  0x7E | LenHi LenLo | 0x90 Source64[8] Source16[2] Options | RFData... | Checksum
       ──── | ──────────── | ──────────────────────────────────── | ───────── | ────────
Start  Delim  Length         Frame Type + Header (12 bytes)         Payload     0xFF - sum
```

---

## Useful XCTU Features

- **Network Discovery:** Click the network icon to see all XBee modules in the PAN
- **Range Test:** Built-in tool to test signal strength and packet loss
- **Firmware Update:** Keep your XBee S2C firmware up to date via XCTU
- **API Frames Console:** Build and decode API frames with visual frame builder
- **Frame Generator:** Create test frames to send to devices
