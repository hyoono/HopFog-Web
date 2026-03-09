/*
 * xbee_comm.cpp — XBee S2C API Mode 1 driver (ESP32-CAM only)
 *
 * Uses UART0 (Serial) on GPIO 1/3 — native IOMUX pins.
 * Code structure matches the working XBEE_COMM_TEST project exactly.
 */

#include "xbee_comm.h"
#include "config.h"
#include <stdarg.h>

// ── Diagnostic counters ─────────────────────────────────────
unsigned long xbTotalRxBytes   = 0;
unsigned long xbTotalTxBytes   = 0;
unsigned long xbTxStatusOk     = 0;
unsigned long xbTxStatusFail   = 0;
unsigned long xbRxFramesParsed = 0;
unsigned long xbSelfEchoCount  = 0;

// ── Message log ─────────────────────────────────────────────
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
static XBeeReceiveCB   rxCallback = nullptr;
static uint8_t         frameIdCounter = 0;

// ── RX state machine ────────────────────────────────────────
enum RxState { WAIT_DELIM, GOT_LEN_HI, GOT_LEN_LO, READING_DATA, GOT_CHECKSUM };

static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// ── Init ────────────────────────────────────────────────────

void xbeeInit() {
    Serial.begin(XBEE_BAUD);
    logMsg('S', "XBee UART0 GPIO1/3 @ %d baud", XBEE_BAUD);
}

// ── TX ──────────────────────────────────────────────────────

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 14) return 0;

    uint16_t frameDataLen = 14 + len;
    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF,   // broadcast 64-bit addr
        0xFF, 0xFE, 0x00, 0x00    // broadcast 16-bit addr
    };

    uint8_t cksum = 0;
    for (int i = 0; i < 14; i++) cksum += hdr[i];
    for (size_t i = 0; i < len; i++) cksum += (uint8_t)payload[i];
    cksum = 0xFF - cksum;

    Serial.write(XBEE_START_DELIM);
    Serial.write((uint8_t)(frameDataLen >> 8));
    Serial.write((uint8_t)(frameDataLen & 0xFF));
    Serial.write(hdr, 14);
    Serial.write((const uint8_t*)payload, len);
    Serial.write(cksum);
    Serial.flush();

    xbTotalTxBytes += 3 + 14 + len + 1;
    logMsg('T', "fid=%d (%d B) %.160s", fid, (int)len, payload);
    return fid;
}

// ── Callback ────────────────────────────────────────────────

void xbeeSetReceiveCallback(XBeeReceiveCB cb) { rxCallback = cb; }

// ── RX processing ───────────────────────────────────────────

void xbeeProcessIncoming() {
    while (Serial.available()) {
        uint8_t b = Serial.read();
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
                } else if (ft == XBEE_MODEM_STATUS && rxFrameLen >= 2) {
                    const char* desc = "unknown";
                    switch (rxFrame[1]) {
                        case 0x00: desc = "Hardware reset"; break;
                        case 0x02: desc = "Joined network"; break;
                        case 0x06: desc = "Coordinator started"; break;
                    }
                    logMsg('S', "Modem: %s (0x%02X)", desc, rxFrame[1]);
                } else if (ft == XBEE_TX_REQUEST) {
                    xbSelfEchoCount++;
                    logMsg('S', "Self-echo (0x10) ignored");
                } else {
                    logMsg('E', "Unknown frame 0x%02X len=%d", ft, rxFrameLen);
                }
            } else {
                logMsg('E', "Checksum error (0x%02X)", rxChecksum);
            }
            rxState = WAIT_DELIM;
            break;
        }
    }
}

// ═════════════════════════════════════════════════════════════
//  Web API functions (for admin dashboard)
// ═════════════════════════════════════════════════════════════

void xbeeGetLog(JsonArray& arr) {
    for (int i = 0; i < msgLogCount; i++) {
        int idx = (msgLogHead - msgLogCount + i + MSG_LOG_SIZE) % MSG_LOG_SIZE;
        const MsgLogEntry& e = msgLog[idx];
        JsonObject o = arr.add<JsonObject>();
        o["ts"]   = e.ts;
        o["dir"]  = String(e.dir);
        o["text"] = e.text;
    }
}

void xbeeGetDiagnostics(JsonObject& diag) {
    diag["tx_bytes"]         = xbTotalTxBytes;
    diag["rx_bytes"]         = xbTotalRxBytes;
    diag["rx_frames_parsed"] = xbRxFramesParsed;
    diag["tx_status_ok"]     = xbTxStatusOk;
    diag["tx_status_fail"]   = xbTxStatusFail;
    diag["self_echo_count"]  = xbSelfEchoCount;
    diag["uptime_ms"]        = millis();
    diag["tx_pin"]           = XBEE_TX_PIN;
    diag["rx_pin"]           = XBEE_RX_PIN;
    diag["baud"]             = XBEE_BAUD;

    if (xbRxFramesParsed > 0)
        diag["assessment"] = "WORKING — receiving data from remote XBee.";
    else if (xbTxStatusOk > 0)
        diag["assessment"] = "TX OK — XBee responds. Waiting for remote device.";
    else if (xbTxStatusFail > 0)
        diag["assessment"] = "TX FAIL — no remote device. Check PAN IDs match.";
    else if (xbTotalRxBytes > 0)
        diag["assessment"] = "RX bytes seen but no complete frames yet.";
    else if (xbTotalTxBytes > 0)
        diag["assessment"] = "TX sent, 0 RX. Check wiring: XBee DOUT→GPIO3.";
    else
        diag["assessment"] = "No communication. Check XBee power and wiring.";
}
