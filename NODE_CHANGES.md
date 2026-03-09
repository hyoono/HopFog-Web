# HopFog-Node — Alignment Changes (2-Module Setup)

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [hyoono/HopFog-Node](https://github.com/hyoono/HopFog-Node)
> Branch: `copilot/setup-and-xbee-driver`
>
> This document SUPERSEDES all previous change instructions (#1–#9).
> It describes alignment changes between admin and node for a 2-module setup
> (1 coordinator admin + 1 router node).

---

## What Changed on the Admin Side (HopFog-Web)

### 1. Admin Now Sends Periodic PINGs

**THE KEY CHANGE.** Previously, the admin sat silently waiting for nodes.
Now the admin broadcasts `{"cmd":"PING","node_id":"admin"}` every 10 seconds.

This means:
- The node knows the admin exists (and can respond with PONG)
- TX Status frames (0x8B) prove the admin's XBee is working
- If the node sees PINGs but admin doesn't see REGISTER, we know the issue is
  admin RX specifically

### 2. XBee Driver Aligned with Node

The admin's `xbee_comm.cpp` now uses the same `XBeeStats` struct as the node.
Previously the admin used individual `extern unsigned long` counters. The
driver is otherwise character-for-character identical to the node's.

### 3. WiFi Channel Aligned

Admin WiFi changed from channel 1 to channel 6 to match the node.
Not critical for XBee (different protocol), but reduces potential 2.4 GHz
interference between WiFi and ZigBee.

### 4. PING/PONG Handling

Admin now handles incoming "PING" commands (responds with PONG) and
incoming "PONG" commands (logs that node is alive). This matches the
working test project's bidirectional heartbeat.

---

## Required Changes for the Node

Apply ALL of the following to the HopFog-Node repository on
branch `copilot/setup-and-xbee-driver`.

### Change 1: Handle Admin PING in `node_client.cpp`

The admin sends `{"cmd":"PING","node_id":"admin"}` every 10 seconds.
The node's `nodeClientHandleCommand()` must handle this.

In `src/node_client.cpp`, find the command dispatcher and add PING handling:

```cpp
// In nodeClientHandleCommand(), add these cases:

    } else if (strcmp(cmd, "PING") == 0) {
        // Admin sent a PING — reply with PONG
        JsonDocument pong;
        pong["cmd"] = "PONG";
        sendCommand(pong);
    } else if (strcmp(cmd, "PONG") == 0) {
        // Admin responded to our heartbeat — admin is alive
        dbgprintln("[Node] Got PONG from admin");
```

Make sure these are added BEFORE the `else { return false; }` fallback.

### Change 2: WiFi Channel to 6

In `include/config.h`, change the WiFi channel to match the admin:

```cpp
#define AP_CHANNEL    6
```

### Change 3: Verify `ets_install_putc1(nullPutc)` is Present

The node's `src/main.cpp` must have this as the VERY FIRST line in `setup()`:

```cpp
#include <rom/ets_sys.h>
#include <esp_log.h>

static void nullPutc(char c) { (void)c; }

void setup() {
    ets_install_putc1(nullPutc);
    esp_log_level_set("*", ESP_LOG_NONE);
    // ... rest of setup ...
}
```

This silences `ets_printf()` which ESP-IDF's WiFi/SPI/AsyncTCP drivers use
internally. Without this, UART0 debug output corrupts XBee frames.

---

## XBee Configuration (XCTU) — 2 Modules

Configure BOTH XBee modules in XCTU before connecting to ESP32-CAMs.

### Admin XBee (Coordinator)

| Parameter | Value | Description |
|-----------|-------|-------------|
| **CE** | **1** | Coordinator Enable |
| **AP** | **1** | API mode 1 (0x7E framed) |
| **BD** | **3** | 9600 baud |
| **ID** | **1234** | PAN ID (any value 0-FFFF, MUST match node) |
| **DH** | **0** | Destination High (broadcast) |
| **DL** | **FFFF** | Destination Low (broadcast) |
| **NI** | **ADMIN** | Node Identifier (optional) |

### Node XBee (Router)

| Parameter | Value | Description |
|-----------|-------|-------------|
| **CE** | **0** | Router (NOT coordinator) |
| **JV** | **1** | Channel Verification (joins coordinator) |
| **AP** | **1** | API mode 1 (0x7E framed) |
| **BD** | **3** | 9600 baud |
| **ID** | **1234** | PAN ID (any value 0-FFFF, MUST match admin) |
| **DH** | **0** | Destination High (broadcast) |
| **DL** | **FFFF** | Destination Low (broadcast) |
| **NI** | **NODE01** | Node Identifier (optional) |

### CRITICAL: Both must have:
- **Same PAN ID** (ID parameter)
- **AP = 1** (API mode, NOT transparent mode)
- **BD = 3** (9600 baud, matching `Serial.begin(9600)`)

### To configure in XCTU:
1. Connect XBee to USB Explorer board (NOT to ESP32-CAM)
2. Open XCTU → Add Radio Module
3. Set parameters as above
4. Click "Write" to save to XBee
5. Repeat for the other XBee
6. Connect to ESP32-CAMs and power on

---

## Verification Checklist

After flashing both devices:

1. Power on admin ESP32-CAM first (coordinator starts network)
2. Power on node ESP32-CAM second (router joins network)
3. Wait 30 seconds for ZigBee network formation

### Check Admin Dashboard
Connect phone to "HopFog-Network" WiFi, open http://192.168.4.1

- ✅ `tx_frames_sent > 0` — Admin is sending PINGs
- ✅ `tx_status_ok > 0` — XBee acknowledged the TX (even without remote)
- ✅ `rx_bytes > 0` — Receiving data from node XBee
- ✅ `rx_frames_parsed > 0` — Complete frames from remote device
- ✅ Node appears in "Registered Nodes" list

### Check Node Status
Connect phone to "HopFog-Node-01" WiFi, open http://192.168.4.1/status

- ✅ `totalRxBytes > 0` — UART0 RX is working
- ✅ `rxFramesParsed > 0` — Admin PING received
- ✅ `state == 3` — STATE_RUNNING (0=unregistered, 1=registered, 2=syncing, 3=running)

### Admin Manual Triggers (NEW)

The admin testing page at `/admin/messaging/testing` now has **manual trigger buttons**:
- **PING** — sends `{"cmd":"PING","node_id":"admin"}`
- **REGISTER_ACK** — sends `{"cmd":"REGISTER_ACK","node_id":"<target>"}`
- **PONG** — sends `{"cmd":"PONG","node_id":"admin"}`
- **GET_STATS** — sends `{"cmd":"GET_STATS","node_id":"admin"}`
- **HEARTBEAT** — sends `{"cmd":"HEARTBEAT","node_id":"admin"}`
- **BROADCAST_MSG** — sends a test broadcast
- **Send SYNC_DATA** — triggers full data sync to the target node

These buttons let you test each protocol step independently, without relying on the
automatic registration flow. If clicking REGISTER_ACK on admin and the node picks
it up and transitions to registered state, it proves the XBee communication works.

### Diagnostic Decision Tree

```
Admin tx_status_ok == 0?
  → XBee not connected or wrong baud. Check wiring + AP=1 + BD=3.

Admin tx_status_ok > 0 but rx_bytes == 0?
  → Admin TX works, admin RX broken.
  → Check: XBee DOUT pin connected to GPIO 3 (U0RXD).
  → Check: ets_install_putc1(nullPutc) is FIRST in setup().

Admin rx_bytes > 0 but rx_frames_parsed == 0?
  → Getting garbage on UART0, not valid XBee frames.
  → Check: BD=3 (9600) on XBee matches Serial.begin(9600).

Node totalRxBytes == 0?
  → Node is not receiving any UART0 data.
  → Check same things as admin above.

Node state == 0 (stuck UNREGISTERED)?
  → Node REGISTER never got REGISTER_ACK from admin.
  → Try clicking REGISTER_ACK on admin testing page manually.
  → Check admin is receiving frames (rx_frames_parsed > 0).
  → Check PAN IDs match on both XBees.
```
