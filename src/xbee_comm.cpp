/*
 * xbee_comm.cpp — XBee S2C API Mode 1 driver (ESP32-CAM only)
 *
 * Uses UART0 (Serial) on GPIO 1/3 — native IOMUX pins.
 *
 * Core driver is CHARACTER-FOR-CHARACTER identical to the working
 * XBEE_COMM_TEST project AND to HopFog-Node's xbee_comm.cpp.
 * Only additions: MsgLogEntry ring buffer + web API functions for
 * the admin dashboard.
 *
 * Uses the same XBeeStats struct as the node for alignment.
 */

#include "xbee_comm.h"
#include "config.h"
#include <stdarg.h>

// ── Stats (same struct as HopFog-Node) ──────────────────────
static XBeeStats stats = {};

// ── Message log (admin-only) ────────────────────────────────
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

// ── RX state machine (identical to test project + node) ─────
enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };

static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// ── Init (identical to test project + node) ─────────────────

void xbeeInit() {
    Serial.begin(XBEE_BAUD);
    memset(&stats, 0, sizeof(stats));
    logMsg('S', "XBee UART0 GPIO1/3 @ %d baud", XBEE_BAUD);
}

// ── TX (identical to test project + node) ───────────────────

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;
    uint16_t frameDataLen = 14 + len;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0xFE, 0x00, 0x00
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

    stats.totalTxBytes += 3 + 14 + len + 1;
    stats.txFramesSent++;
    logMsg('T', "fid=%d (%d B) %.160s", fid, (int)len, payload);
    return fid;
}

// ── Callback ────────────────────────────────────────────────

void xbeeSetReceiveCallback(XBeeReceiveCB cb) { rxCallback = cb; }
const XBeeStats& xbeeGetStats() { return stats; }

// ── Frame handler (identical to node) ───────────────────────

static void handleCompleteFrame() {
    stats.rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    if (ft == XBEE_RX_PACKET && rxFrameLen >= 13) {
        const char* rfData = (const char*)&rxFrame[12];
        size_t rfLen = rxFrameLen - 12;
        while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) rfLen--;
        if (rfLen > 0) {
            rxFrame[12 + rfLen] = '\0';
            logMsg('R', "(%d B) %.160s", (int)rfLen, rfData);
            if (rxCallback) rxCallback(rfData, rfLen);
        }
    } else if (ft == XBEE_TX_STATUS && rxFrameLen >= 7) {
        if (rxFrame[5] == 0x00) {
            stats.txStatusOK++;
            logMsg('S', "TX OK fid=%d", rxFrame[1]);
        } else {
            stats.txStatusFail++;
            logMsg('E', "TX FAIL fid=%d err=0x%02X", rxFrame[1], rxFrame[5]);
        }
    } else if (ft == XBEE_MODEM_STATUS && rxFrameLen >= 2) {
        stats.modemStatusCount++;
        stats.lastModemStatus = rxFrame[1];
        const char* desc = "unknown";
        switch (rxFrame[1]) {
            case 0x00: desc = "Hardware reset"; break;
            case 0x02: desc = "Joined network"; break;
            case 0x06: desc = "Coordinator started"; break;
        }
        logMsg('S', "Modem: %s (0x%02X)", desc, rxFrame[1]);
    }
    // 0x10 self-echo and others: silently ignore
}

// ── RX processing (identical to test project + node) ────────

void xbeeProcessIncoming() {
    while (Serial.available()) {
        uint8_t b = Serial.read();
        stats.totalRxBytes++;

        switch (rxState) {
        case WAIT_DELIM:
            if (b == XBEE_START_DELIM) rxState = LEN_HI;
            break;
        case LEN_HI:
            rxFrameLen = (uint16_t)b << 8;
            rxState = LEN_LO;
            break;
        case LEN_LO:
            rxFrameLen |= b;
            if (rxFrameLen == 0 || rxFrameLen >= XBEE_MAX_FRAME) { rxState = WAIT_DELIM; }
            else { rxIdx = 0; rxChecksum = 0; rxState = FRAME_DATA; }
            break;
        case FRAME_DATA:
            rxFrame[rxIdx++] = b;
            rxChecksum += b;
            if (rxIdx >= rxFrameLen) rxState = CHECKSUM;
            break;
        case CHECKSUM:
            rxChecksum += b;
            if (rxChecksum == 0xFF) handleCompleteFrame();
            rxState = WAIT_DELIM;
            break;
        }
    }
}

// ═════════════════════════════════════════════════════════════
//  Web API functions (admin dashboard only — not in node)
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
    diag["tx_bytes"]         = stats.totalTxBytes;
    diag["rx_bytes"]         = stats.totalRxBytes;
    diag["rx_frames_parsed"] = stats.rxFramesParsed;
    diag["tx_frames_sent"]   = stats.txFramesSent;
    diag["tx_status_ok"]     = stats.txStatusOK;
    diag["tx_status_fail"]   = stats.txStatusFail;
    diag["modem_status_count"] = stats.modemStatusCount;
    diag["last_modem_status"]  = stats.lastModemStatus;
    diag["uptime_ms"]        = millis();
    diag["tx_pin"]           = XBEE_TX_PIN;
    diag["rx_pin"]           = XBEE_RX_PIN;
    diag["baud"]             = XBEE_BAUD;

    if (stats.rxFramesParsed > 0)
        diag["assessment"] = "WORKING — receiving data from remote XBee.";
    else if (stats.txStatusOK > 0)
        diag["assessment"] = "TX OK — XBee responds. Waiting for remote device.";
    else if (stats.txStatusFail > 0)
        diag["assessment"] = "TX FAIL — no remote device. Check PAN IDs match.";
    else if (stats.totalRxBytes > 0)
        diag["assessment"] = "RX bytes seen but no complete frames yet.";
    else if (stats.totalTxBytes > 0)
        diag["assessment"] = "TX sent, 0 RX. Check wiring: XBee DOUT→GPIO3.";
    else
        diag["assessment"] = "No communication. Check XBee power and wiring.";
}
