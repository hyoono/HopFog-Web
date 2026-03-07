# ESP32-CAM + XBee S2C Communication Test

> **Purpose:** Generate a minimal PlatformIO project that tests XBee communication
> between two ESP32-CAM boards. Each device runs a **WiFi AP + web dashboard**
> so you can monitor all XBee traffic from your phone/laptop browser.
>
> **Give this entire file as a prompt to any AI coding assistant** (Copilot, Claude,
> Gemini, ChatGPT) and ask it to create the project.

---

## What This Tests

Two ESP32-CAM boards, each connected to an XBee S2C module, communicate over
ZigBee using XBee API mode 1 (binary-framed packets). The test validates:

1. **PING / PONG** — Device A sends `{"cmd":"PING"}`, Device B replies `{"cmd":"PONG"}`
2. **REGISTER / REGISTER_ACK** — Device B sends `{"cmd":"REGISTER","node_id":"node-01"}`,
   Device A replies `{"cmd":"REGISTER_ACK","node_id":"node-01"}`
3. **HEARTBEAT / PONG** — Device B sends `{"cmd":"HEARTBEAT","node_id":"node-01"}`,
   Device A replies `{"cmd":"PONG","node_id":"node-01"}`

Device A = **Coordinator** (admin).  Device B = **Router** (node).
Both run the SAME firmware — the role is determined by each XBee's CE setting.

**Each device creates its own WiFi network** with a live web dashboard showing
all XBee traffic, counters, and manual send controls. No USB Serial Monitor needed.

---

## Hardware Setup

### Per device (×2)
- 1× AI-Thinker ESP32-CAM board
- 1× Digi XBee S2C (or S2C Pro) module
- 1× XBee breakout board (e.g., Sparkfun XBee Explorer)
- 4 jumper wires

### Wiring (identical for both devices)

```
ESP32-CAM          XBee Module
─────────          ───────────
GPIO 1 (U0TXD) ──→ DIN  (pin 3)
GPIO 3 (U0RXD) ←── DOUT (pin 2)
3.3V            ──→ VCC  (pin 1)
GND             ──→ GND  (pin 10)
```

> **IMPORTANT:** GPIO 1 and 3 are UART0 (the USB serial port).
> Disconnect the XBee before uploading firmware via USB.
> Reconnect the XBee after upload. USB Serial Monitor will NOT work —
> use the WiFi web dashboard instead.

### XBee Configuration (via XCTU)

**Device A — Coordinator:**
| Setting | Value | Description |
|---------|-------|-------------|
| CE | 1 | Coordinator Enable |
| AP | 1 | API mode 1 (no escapes) |
| ID | 1234 | PAN ID (must match) |
| BD | 3 | 9600 baud |
| DH | 0 | Destination High (broadcast) |
| DL | FFFF | Destination Low (broadcast) |

**Device B — Router:**
| Setting | Value | Description |
|---------|-------|-------------|
| CE | 0 | Join as Router |
| JV | 1 | Channel Verification (join coordinator's network) |
| AP | 1 | API mode 1 (no escapes) |
| ID | 1234 | PAN ID (must match Device A) |
| BD | 3 | 9600 baud |
| DH | 0 | Destination High (broadcast) |
| DL | FFFF | Destination Low (broadcast) |

> After configuring each XBee, click "Write" in XCTU. Remove from XCTU and
> connect to the ESP32-CAM.

---

## WiFi Web Dashboard

Each device creates its own WiFi AP:
- **Device A:** SSID = `XBee-Test-A`, password = `xbeetest123`
- **Device B:** SSID = `XBee-Test-B`, password = `xbeetest123`

(Both use the same firmware. The SSID suffix is derived from each device's unique MAC address.)

Connect your phone/laptop to the WiFi and open **http://192.168.4.1** in a browser.

The dashboard shows:
- **Status panel:** Role (Coordinator/Router/Unknown), registered status, uptime
- **Counters:** TX bytes, RX bytes, TX status OK/fail, messages received
- **Live message log:** Scrolling list of all sent/received XBee messages with timestamps
- **Send buttons:** Manually send PING, REGISTER, or custom JSON

---

## Project Structure

Create a PlatformIO project with these files:

```
xbee-comm-test/
├── platformio.ini
├── src/
│   ├── main.cpp
│   └── xbee_comm.cpp
├── include/
│   ├── xbee_comm.h
│   └── config.h
└── lib/
    └── (empty)
```

---

## File: `platformio.ini`

```ini
[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino
monitor_speed = 115200
lib_deps =
    me-no-dev/ESPAsyncWebServer@^1.2.4
    me-no-dev/AsyncTCP@^1.1.1
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM=1
```

---

## File: `include/config.h`

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// ── XBee Pin Assignment ──────────────────────────────────────
// ESP32-CAM: UART0 native pins (IOMUX, no GPIO matrix remapping)
#define XBEE_TX_PIN   1    // U0TXD → XBee DIN (pin 3)
#define XBEE_RX_PIN   3    // U0RXD ← XBee DOUT (pin 2)
#define XBEE_BAUD     9600

// ── LED ──────────────────────────────────────────────────────
// ESP32-CAM built-in red LED (active LOW on GPIO 33)
#define LED_PIN       33
#define LED_ON        LOW
#define LED_OFF       HIGH

// Flash LED on GPIO 4 — force OFF
#define FLASH_LED_PIN 4

// ── WiFi AP ──────────────────────────────────────────────────
#define WIFI_AP_PASS  "xbeetest123"

// ── Timing ───────────────────────────────────────────────────
#define PING_INTERVAL_MS      5000   // Send PING every 5 seconds
#define HEARTBEAT_INTERVAL_MS 10000  // Send HEARTBEAT every 10 seconds
#define REGISTER_INTERVAL_MS  8000   // Re-send REGISTER if no ACK

// ── Message log ──────────────────────────────────────────────
#define MSG_LOG_SIZE  60   // ring buffer capacity

struct MsgLogEntry {
    unsigned long ts;       // millis()
    char dir;               // 'T' = TX, 'R' = RX, 'S' = status, 'E' = error
    char text[200];         // message content
};

#endif // CONFIG_H
```

---

## File: `include/xbee_comm.h`

```cpp
#ifndef XBEE_COMM_H
#define XBEE_COMM_H

#include <Arduino.h>
#include "config.h"

// ── XBee API Mode 1 Constants ────────────────────────────────
#define XBEE_START_DELIM   0x7E
#define XBEE_TX_REQUEST    0x10  // Transmit Request frame type
#define XBEE_RX_PACKET     0x90  // Receive Packet frame type
#define XBEE_TX_STATUS     0x8B  // Transmit Status frame type

#define XBEE_MAX_FRAME     512

// Callback: called when a 0x90 Receive Packet frame arrives.
typedef void (*XBeeReceiveCB)(const char* payload, size_t len);

void xbeeInit();
uint8_t xbeeSendBroadcast(const char* payload, size_t len);
inline uint8_t xbeeSendBroadcast(const char* text) {
    return xbeeSendBroadcast(text, strlen(text));
}
void xbeeSetReceiveCallback(XBeeReceiveCB cb);
void xbeeProcessIncoming();

// ── Diagnostics (accessible from main.cpp for web API) ──────
extern unsigned long xbTotalRxBytes;
extern unsigned long xbTotalTxBytes;
extern unsigned long xbTxStatusOk;
extern unsigned long xbTxStatusFail;
extern unsigned long xbRxFramesParsed;
extern unsigned long xbSelfEchoCount;

// ── Message log (shared ring buffer for web dashboard) ───────
extern MsgLogEntry msgLog[MSG_LOG_SIZE];
extern int msgLogHead;
extern int msgLogCount;
void logMsg(char dir, const char* fmt, ...);

#endif // XBEE_COMM_H
```

---

## File: `src/xbee_comm.cpp`

```cpp
#include "xbee_comm.h"
#include <stdarg.h>

// ── Shared diagnostic counters ──────────────────────────────
unsigned long xbTotalRxBytes   = 0;
unsigned long xbTotalTxBytes   = 0;
unsigned long xbTxStatusOk     = 0;
unsigned long xbTxStatusFail   = 0;
unsigned long xbRxFramesParsed = 0;
unsigned long xbSelfEchoCount  = 0;

// ── Shared message log ──────────────────────────────────────
MsgLogEntry msgLog[MSG_LOG_SIZE];
int msgLogHead  = 0;
int msgLogCount = 0;

void logMsg(char dir, const char* fmt, ...) {
    MsgLogEntry& e = msgLog[msgLogHead];
    e.ts = millis();
    e.dir = dir;
    va_list args;
    va_start(args, fmt);
    vsnprintf(e.text, sizeof(e.text), fmt, args);
    va_end(args);
    msgLogHead = (msgLogHead + 1) % MSG_LOG_SIZE;
    if (msgLogCount < MSG_LOG_SIZE) msgLogCount++;
}

// ── Internal state ──────────────────────────────────────────
static HardwareSerial& xbeeSerial = Serial;  // UART0 on GPIO 1/3
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;

// ── Frame receive state machine ─────────────────────────────
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };

static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// ── Public API ──────────────────────────────────────────────

void xbeeInit() {
    xbeeSerial.begin(XBEE_BAUD);
    logMsg('S', "XBee init: TX=GPIO%d RX=GPIO%d baud=%d", XBEE_TX_PIN, XBEE_RX_PIN, XBEE_BAUD);
}

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE, 0x00, 0x00
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    xbeeSerial.write(XBEE_START_DELIM);
    xbeeSerial.write((uint8_t)(frameDataLen >> 8));
    xbeeSerial.write((uint8_t)(frameDataLen & 0xFF));
    xbeeSerial.write(hdr, 14);
    xbeeSerial.write((const uint8_t*)payload, len);
    xbeeSerial.write(cksum);
    xbeeSerial.flush();

    xbTotalTxBytes += 3 + 14 + len + 1;
    logMsg('T', "fid=%d (%d B) %.160s", fid, (int)len, payload);
    return fid;
}

void xbeeSetReceiveCallback(XBeeReceiveCB cb) { rxCallback = cb; }

void xbeeProcessIncoming() {
    while (xbeeSerial.available()) {
        uint8_t b = xbeeSerial.read();
        xbTotalRxBytes++;

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = GOT_LEN_HI;
            break;
        case GOT_LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = GOT_LEN_LO;
            break;
        case GOT_LEN_LO:
            rxFrameLen |= b;
            rxIdx = 0; rxChecksum = 0;
            rxState = (rxFrameLen > 0 && rxFrameLen < XBEE_MAX_FRAME)
                      ? READING_DATA : WAIT_DELIM;
            break;
        case READING_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = GOT_CHECKSUM;
            break;
        case GOT_CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) {
                uint8_t ft = rxFrame[0];
                if (ft == XBEE_RX_PACKET && rxFrameLen >= 13) {
                    xbRxFramesParsed++;
                    const char* d = (const char*)&rxFrame[12];
                    size_t dLen = rxFrameLen - 12;
                    while (dLen > 0 && (d[dLen-1]=='\n'||d[dLen-1]=='\r')) dLen--;
                    if (dLen > 0 && dLen < XBEE_MAX_FRAME - 12) {
                        rxFrame[12 + dLen] = '\0';
                        logMsg('R', "(%d B) %.160s", (int)dLen, d);
                        if (rxCallback) rxCallback(d, dLen);
                    }
                } else if (ft == XBEE_TX_STATUS && rxFrameLen >= 7) {
                    uint8_t del = rxFrame[5];
                    if (del == 0) { xbTxStatusOk++; logMsg('S', "TX OK fid=%d", rxFrame[1]); }
                    else { xbTxStatusFail++; logMsg('E', "TX FAIL fid=%d err=0x%02X", rxFrame[1], del); }
                } else if (ft == XBEE_TX_REQUEST) {
                    xbSelfEchoCount++;
                    logMsg('S', "Self-echo (0x10) ignored");
                } else {
                    logMsg('E', "Unknown frame 0x%02X len=%d", ft, rxFrameLen);
                }
            } else {
                logMsg('E', "Checksum error (got 0x%02X)", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}
```

---

## File: `src/main.cpp`

```cpp
/*
 * ESP32-CAM + XBee S2C Communication Test — with WiFi Web Dashboard
 *
 * Both devices run the SAME firmware.
 * Role auto-detected: first to receive REGISTER becomes Coordinator.
 *
 * WiFi AP: "XBee-Test-XXXX" (random suffix), password: xbeetest123
 * Dashboard: http://192.168.4.1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "xbee_comm.h"

AsyncWebServer server(80);

// ── State ───────────────────────────────────────────────────
static bool isRegistered  = false;
static bool isCoordinator = false;
static unsigned long lastPingSent      = 0;
static unsigned long lastRegisterSent  = 0;
static unsigned long lastHeartbeatSent = 0;
static unsigned long lastRxTime        = 0;
static int rxCount = 0;
static char apSSID[24] = "XBee-Test";

// LED helpers
static void ledOn()  { digitalWrite(LED_PIN, LED_ON); }
static void ledOff() { digitalWrite(LED_PIN, LED_OFF); }
static void blinkLed(int n, int ms) {
    for (int i = 0; i < n; i++) { ledOn(); delay(ms); ledOff(); delay(ms); }
}

// ── XBee receive callback ───────────────────────────────────

static void onXBeeReceive(const char* payload, size_t len) {
    rxCount++;
    lastRxTime = millis();
    blinkLed(1, 80);

    if (strstr(payload, "\"cmd\":\"PING\"")) {
        xbeeSendBroadcast("{\"cmd\":\"PONG\"}");
    } else if (strstr(payload, "\"cmd\":\"REGISTER\"") &&
               !strstr(payload, "\"cmd\":\"REGISTER_ACK\"")) {
        isCoordinator = true;
        xbeeSendBroadcast("{\"cmd\":\"REGISTER_ACK\",\"node_id\":\"node-01\"}");
        ledOn(); delay(1000); ledOff();
    } else if (strstr(payload, "\"cmd\":\"REGISTER_ACK\"")) {
        isRegistered = true;
        ledOn(); delay(1000); ledOff();
    } else if (strstr(payload, "\"cmd\":\"HEARTBEAT\"")) {
        isCoordinator = true;
        xbeeSendBroadcast("{\"cmd\":\"PONG\",\"node_id\":\"node-01\"}");
    }
}

// ── HTML Dashboard (embedded) ───────────────────────────────

static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XBee Test</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;padding:10px}
h1{color:#0ff;font-size:1.3em;margin-bottom:10px}
.panel{background:#16213e;border:1px solid #0f3460;border-radius:8px;padding:12px;margin-bottom:10px}
.panel h2{color:#e94560;font-size:1em;margin-bottom:8px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:6px}
.stat{background:#0f3460;padding:6px 10px;border-radius:4px;font-size:0.85em}
.stat b{color:#0ff}
.log{background:#0a0a1a;border:1px solid #333;border-radius:4px;height:300px;overflow-y:auto;
     font-size:0.8em;padding:6px;margin-top:6px}
.log div{padding:2px 0;border-bottom:1px solid #222}
.T{color:#ff0}
.R{color:#0f0}
.E{color:#f44}
.S{color:#0ff}
.btn{background:#0f3460;color:#fff;border:1px solid #0ff;border-radius:4px;padding:8px 14px;
     cursor:pointer;font-family:monospace;font-size:0.85em;margin:3px}
.btn:hover{background:#e94560}
.btn:active{background:#c0392b}
input[type=text]{background:#0a0a1a;color:#0f0;border:1px solid #555;border-radius:4px;
                 padding:8px;font-family:monospace;width:100%;margin:4px 0}
.ok{color:#0f0} .fail{color:#f44} .warn{color:#ff0}
</style></head><body>
<h1>&#x1F4E1; XBee Communication Test</h1>

<div class="panel">
<h2>Status</h2>
<div class="row">
 <div class="stat">Role: <b id="role">?</b></div>
 <div class="stat">Registered: <b id="reg">?</b></div>
 <div class="stat">Uptime: <b id="up">0s</b></div>
 <div class="stat">RX msgs: <b id="rxc">0</b></div>
</div>
<div class="row">
 <div class="stat">TX bytes: <b id="txb">0</b></div>
 <div class="stat">RX bytes: <b id="rxb">0</b></div>
 <div class="stat">TX OK: <b id="txok" class="ok">0</b></div>
 <div class="stat">TX Fail: <b id="txf" class="fail">0</b></div>
 <div class="stat">Self-echo: <b id="se">0</b></div>
 <div class="stat">RX frames: <b id="rxfp">0</b></div>
</div>
<div id="assess" style="margin-top:6px;padding:6px;border-radius:4px;background:#0a0a1a"></div>
</div>

<div class="panel">
<h2>Send</h2>
<div class="row">
 <button class="btn" onclick="send('PING')">&#x1F3D3; PING</button>
 <button class="btn" onclick="send('REGISTER')">&#x1F4DD; REGISTER</button>
 <button class="btn" onclick="send('HEARTBEAT')">&#x1F493; HEARTBEAT</button>
</div>
<input type="text" id="custom" placeholder='Custom JSON, e.g. {"cmd":"PING"}'>
<button class="btn" onclick="sendCustom()">&#x1F4E8; Send Custom</button>
</div>

<div class="panel">
<h2>Message Log <button class="btn" onclick="clearLog()" style="float:right;padding:4px 8px">Clear</button></h2>
<div class="log" id="log"></div>
</div>

<script>
var logDiv=document.getElementById('log');
var prevCount=0;

function poll(){
 fetch('/api/status').then(r=>r.json()).then(d=>{
  document.getElementById('role').textContent=d.role;
  document.getElementById('reg').textContent=d.registered?'Yes':'No';
  document.getElementById('reg').className=d.registered?'ok':'warn';
  document.getElementById('up').textContent=d.uptime_s+'s';
  document.getElementById('rxc').textContent=d.rx_count;
  document.getElementById('txb').textContent=d.tx_bytes;
  document.getElementById('rxb').textContent=d.rx_bytes;
  document.getElementById('txok').textContent=d.tx_ok;
  document.getElementById('txf').textContent=d.tx_fail;
  document.getElementById('se').textContent=d.self_echo;
  document.getElementById('rxfp').textContent=d.rx_frames;
  var a=document.getElementById('assess');
  if(d.rx_bytes==0){a.innerHTML='<span class="fail">&#x274C; No bytes received from XBee. Check wiring.</span>';}
  else if(d.rx_frames>0){a.innerHTML='<span class="ok">&#x2705; XBee communication working!</span>';}
  else if(d.tx_ok>0){a.innerHTML='<span class="warn">&#x26A0; TX OK but no RX data frames yet. Waiting for remote device.</span>';}
  else{a.innerHTML='<span class="warn">&#x23F3; Connecting...</span>';}
 }).catch(()=>{});
}

function pollLog(){
 fetch('/api/log').then(r=>r.json()).then(entries=>{
  if(entries.length!==prevCount){
   prevCount=entries.length;
   logDiv.innerHTML='';
   entries.forEach(e=>{
    var div=document.createElement('div');
    div.className=e.dir;
    var sec=(e.ts/1000).toFixed(1);
    var arrow=e.dir==='T'?'TX ->':e.dir==='R'?'RX <-':e.dir==='E'?'ERR !':'SYS *';
    div.textContent=sec+'s ['+arrow+'] '+e.text;
    logDiv.appendChild(div);
   });
   logDiv.scrollTop=logDiv.scrollHeight;
  }
 }).catch(()=>{});
}

function send(cmd){
 fetch('/api/send?cmd='+cmd,{method:'POST'}).then(()=>setTimeout(pollLog,500));
}
function sendCustom(){
 var t=document.getElementById('custom').value;
 if(!t)return;
 fetch('/api/send-raw',{method:'POST',headers:{'Content-Type':'text/plain'},body:t})
 .then(()=>{document.getElementById('custom').value='';setTimeout(pollLog,500);});
}
function clearLog(){
 fetch('/api/clear-log',{method:'POST'}).then(()=>{logDiv.innerHTML='';prevCount=0;});
}

setInterval(poll,2000);
setInterval(pollLog,2000);
poll(); pollLog();
</script>
</body></html>
)rawhtml";

// ── Web server routes ───────────────────────────────────────

static void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send_P(200, "text/html", HTML_PAGE);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req){
        char json[300];
        snprintf(json, sizeof(json),
            "{\"role\":\"%s\",\"registered\":%s,\"uptime_s\":%lu,"
            "\"rx_count\":%d,\"tx_bytes\":%lu,\"rx_bytes\":%lu,"
            "\"tx_ok\":%lu,\"tx_fail\":%lu,\"self_echo\":%lu,\"rx_frames\":%lu}",
            isCoordinator ? "Coordinator" : (isRegistered ? "Node (registered)" : "Unknown"),
            (isRegistered || isCoordinator) ? "true" : "false",
            millis() / 1000,
            rxCount, xbTotalTxBytes, xbTotalRxBytes,
            xbTxStatusOk, xbTxStatusFail, xbSelfEchoCount, xbRxFramesParsed);
        req->send(200, "application/json", json);
    });

    server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req){
        String out = "[";
        for (int i = 0; i < msgLogCount; i++) {
            int idx = (msgLogHead - msgLogCount + i + MSG_LOG_SIZE) % MSG_LOG_SIZE;
            const MsgLogEntry& e = msgLog[idx];
            if (i > 0) out += ",";
            // Escape backslashes first, then quotes
            String escaped = e.text;
            escaped.replace("\"", "\\\"");
            out += "{\"ts\":" + String(e.ts) +
                   ",\"dir\":\"" + String(e.dir) +
                   "\",\"text\":\"" + escaped + "\"}";
        }
        out += "]";
        req->send(200, "application/json", out);
    });

    server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest* req){
        if (req->hasParam("cmd")) {
            String cmd = req->getParam("cmd")->value();
            char buf[200];
            if (cmd == "PING") {
                xbeeSendBroadcast("{\"cmd\":\"PING\"}");
            } else if (cmd == "REGISTER") {
                xbeeSendBroadcast("{\"cmd\":\"REGISTER\",\"node_id\":\"node-01\","
                                  "\"params\":{\"device_name\":\"ESP32CAM-Test\"}}");
            } else if (cmd == "HEARTBEAT") {
                snprintf(buf, sizeof(buf),
                         "{\"cmd\":\"HEARTBEAT\",\"node_id\":\"node-01\","
                         "\"params\":{\"uptime\":%lu}}", millis()/1000);
                xbeeSendBroadcast(buf);
            }
        }
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/send-raw", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (len > 0 && len < 480) {
                char buf[480];
                memcpy(buf, data, len);
                buf[len] = '\0';
                xbeeSendBroadcast(buf);
            }
            req->send(200, "text/plain", "OK");
        });

    server.on("/api/clear-log", HTTP_POST, [](AsyncWebServerRequest* req){
        msgLogHead = 0;
        msgLogCount = 0;
        req->send(200, "text/plain", "OK");
    });

    server.begin();
}

// ── Arduino setup & loop ────────────────────────────────────

void setup() {
    // Turn off flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    // Status LED
    pinMode(LED_PIN, OUTPUT);
    ledOff();
    blinkLed(3, 300);

    // XBee init on UART0 (GPIO 1/3)
    xbeeInit();
    xbeeSetReceiveCallback(onXBeeReceive);

    // WiFi AP — unique SSID per device
    snprintf(apSSID, sizeof(apSSID), "XBee-Test-%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, WIFI_AP_PASS);

    logMsg('S', "WiFi AP: %s  IP: 192.168.4.1", apSSID);
    logMsg('S', "Waiting for XBee communication...");

    // Web server
    setupWebServer();

    // Wait for XBee to join network
    delay(3000);
    blinkLed(5, 100);
}

void loop() {
    xbeeProcessIncoming();

    unsigned long now = millis();

    // Phase 1: Discovery — send PINGs and try to REGISTER
    if (rxCount == 0) {
        if (now - lastPingSent >= PING_INTERVAL_MS) {
            xbeeSendBroadcast("{\"cmd\":\"PING\"}");
            lastPingSent = now;
        }
        if (now - lastRegisterSent >= REGISTER_INTERVAL_MS) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"REGISTER\",\"node_id\":\"%s\","
                     "\"params\":{\"device_name\":\"%s\"}}", apSSID, apSSID);
            xbeeSendBroadcast(buf);
            lastRegisterSent = now;
        }
    }

    // Phase 2: Registered node sends heartbeats
    if (!isCoordinator && isRegistered) {
        if (now - lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"HEARTBEAT\",\"node_id\":\"%s\","
                     "\"params\":{\"uptime\":%lu}}", apSSID, millis()/1000);
            xbeeSendBroadcast(buf);
            lastHeartbeatSent = now;
        }
    }

    // Phase 3: Unregistered node keeps trying
    if (!isCoordinator && !isRegistered && rxCount > 0) {
        if (now - lastRegisterSent >= REGISTER_INTERVAL_MS) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"REGISTER\",\"node_id\":\"%s\","
                     "\"params\":{\"device_name\":\"%s\"}}", apSSID, apSSID);
            xbeeSendBroadcast(buf);
            lastRegisterSent = now;
        }
    }

    // Phase 4: Coordinator pings periodically
    if (isCoordinator && now - lastPingSent >= PING_INTERVAL_MS * 2) {
        xbeeSendBroadcast("{\"cmd\":\"PING\"}");
        lastPingSent = now;
    }

    delay(10);
}
```

---

## How to Use

### 1. Upload firmware
1. **Disconnect XBee** from the ESP32-CAM
2. Connect ESP32-CAM to USB-to-serial adapter
3. Hold BOOT, press RESET → download mode
4. `pio run -e esp32cam --target upload`
5. Press RESET after upload
6. Repeat for second device

### 2. Connect XBees
Wire each XBee to its ESP32-CAM (GPIO 1→DIN, GPIO 3←DOUT, 3.3V, GND).

### 3. Power on both devices
Watch the red LED: 3 slow blinks → 3s pause → 5 fast blinks = ready.

### 4. Open the web dashboard
1. On your phone/laptop, scan for WiFi networks
2. You'll see **`XBee-Test-XXXX`** — connect with password `xbeetest123`
3. Open **http://192.168.4.1** in your browser
4. The dashboard auto-refreshes every 2 seconds

### 5. Watch it work
- The **Message Log** shows every TX/RX with timestamps
- **Status panel** shows role, counters, and a connection assessment
- Use the **Send buttons** to manually trigger PING/REGISTER/HEARTBEAT
- The **RX bytes** counter should increase if XBee wiring is correct

---

## Success Criteria

✅ Dashboard shows "TX OK" count > 0 (XBee accepted our frames)
✅ Dashboard shows "RX bytes" > 0 (XBee is sending data to ESP32)
✅ Dashboard shows "RX frames" > 0 (received valid 0x90 packets from the other device)
✅ Role changes to "Coordinator" or "Node (registered)" after REGISTER_ACK exchange
✅ Message log shows interleaved TX→ and RX← entries

---

## Troubleshooting

| Dashboard shows | Meaning | Fix |
|----------------|---------|-----|
| TX bytes > 0, RX bytes = 0 | ESP32 can't read from XBee | Check GPIO 3 ← XBee DOUT wiring |
| TX OK = 0, TX bytes > 0 | XBee not responding | Check GPIO 1 → XBee DIN, XBee power, AP=1 |
| RX bytes > 0, RX frames = 0 | Receiving garbage | Both XBees must be AP=1 (API mode 1) |
| Self-echo > 0, RX frames = 0 | TX/RX pins bridged | Verify GPIO 1→DIN and GPIO 3←DOUT (not swapped) |
| Everything 0 | No XBee communication | Check all 4 wires, XBee LED should blink |
| Can't connect to WiFi | AP not started | Re-upload firmware (disconnect XBee first) |
