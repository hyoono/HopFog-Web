/*
 * xbee_comm.cpp — XBee S2C API Mode 1 driver (ESP32-CAM only)
 *
 * Uses UART0 (Serial) on GPIO 1/3 — native IOMUX pins.
 *
 * CRITICAL: ZigBee broadcast max RF payload is ~84 bytes (NP parameter).
 * Broadcast frames CANNOT be fragmented — oversized frames are silently
 * dropped by the XBee module. Use xbeeSendTo() for unicast, which
 * supports fragmentation up to ~255 bytes.
 */

#include "xbee_comm.h"
#include "config.h"
#include <stdarg.h>

// ── Stats ───────────────────────────────────────────────────
static XBeeStats stats = {};

// ── Source address from last received 0x90 frame ────────────
static XBeeAddr lastSource = {};

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

// ── RX state machine ────────────────────────────────────────
enum RxState { WAIT_DELIM, LEN_HI, LEN_LO, FRAME_DATA, CHECKSUM };

static RxState  rxState     = WAIT_DELIM;
static uint16_t rxFrameLen  = 0;
static uint16_t rxIdx       = 0;
static uint8_t  rxFrame[XBEE_MAX_FRAME];
static uint8_t  rxChecksum  = 0;

// ── Init ────────────────────────────────────────────────────

void xbeeInit() {
    Serial.begin(XBEE_BAUD);
    memset(&stats, 0, sizeof(stats));
    memset(&lastSource, 0, sizeof(lastSource));
    logMsg('S', "XBee UART0 GPIO1/3 @ %d baud", XBEE_BAUD);
}

// ── Internal: send a TX Request frame to a specific address ─

static uint8_t sendFrame(const uint8_t* dest64, uint16_t dest16,
                         const char* payload, size_t len) {
    if (len == 0 || len > XBEE_MAX_FRAME - 18) return 0;

    if (++frameIdCounter == 0) frameIdCounter = 1;
    uint8_t fid = frameIdCounter;
    uint16_t frameDataLen = 14 + len;

    uint8_t hdr[14] = {
        XBEE_TX_REQUEST, fid,
        dest64[0], dest64[1], dest64[2], dest64[3],
        dest64[4], dest64[5], dest64[6], dest64[7],
        (uint8_t)(dest16 >> 8), (uint8_t)(dest16 & 0xFF),
        0x00, 0x00  // broadcast radius, options
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
    return fid;
}

// ── TX: Broadcast (max ~72 bytes — no fragmentation) ────────

uint8_t xbeeSendBroadcast(const char* payload, size_t len) {
    // WARN if payload exceeds safe broadcast limit
    if (len > XBEE_BROADCAST_MAX) {
        stats.oversizedDrops++;
        logMsg('E', "OVERSIZED BROADCAST! %d B > %d B limit — WILL BE DROPPED BY XBEE",
               (int)len, XBEE_BROADCAST_MAX);
        // Still try to send — the XBee will return TX FAIL (0x74)
        // This lets the user see the failure in diagnostics.
    }

    static const uint8_t broadcastAddr64[8] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
    };
    uint8_t fid = sendFrame(broadcastAddr64, 0xFFFE, payload, len);
    if (fid > 0) {
        const char* sizeWarn = (len > XBEE_BROADCAST_MAX) ? " [OVERSIZED!]" : "";
        logMsg('T', "BC fid=%d (%d B)%s %.140s", fid, (int)len, sizeWarn, payload);
    }
    return fid;
}

// ── TX: Unicast (supports fragmentation, ~255 bytes) ────────

uint8_t xbeeSendTo(const XBeeAddr& dest, const char* payload, size_t len) {
    if (!dest.valid) {
        logMsg('E', "Cannot unicast — no source address received yet");
        return 0;
    }

    uint8_t fid = sendFrame(dest.addr64, dest.addr16, payload, len);
    if (fid > 0) {
        logMsg('T', "UC fid=%d (%d B) to %02X%02X→ %.130s",
               fid, (int)len,
               dest.addr64[6], dest.addr64[7],
               payload);
    }
    return fid;
}

// ── Callback / accessors ────────────────────────────────────

void xbeeSetReceiveCallback(XBeeReceiveCB cb) { rxCallback = cb; }
const XBeeStats& xbeeGetStats() { return stats; }
const XBeeAddr& xbeeGetLastSource() { return lastSource; }

// ── Frame handler ───────────────────────────────────────────

static void handleCompleteFrame() {
    stats.rxFramesParsed++;
    uint8_t ft = rxFrame[0];

    if (ft == XBEE_RX_PACKET && rxFrameLen >= 13) {
        // Extract source address from 0x90 frame
        //   Bytes 1-8:  64-bit source address
        //   Bytes 9-10: 16-bit source address
        //   Byte 11:    receive options
        //   Bytes 12+:  RF data (payload)
        XBeeAddr src;
        memcpy(src.addr64, &rxFrame[1], 8);
        src.addr16 = ((uint16_t)rxFrame[9] << 8) | rxFrame[10];
        src.valid = true;

        // Store as last known source (for unicast replies)
        lastSource = src;

        const char* rfData = (const char*)&rxFrame[12];
        size_t rfLen = rxFrameLen - 12;
        while (rfLen > 0 && (rfData[rfLen - 1] == '\n' || rfData[rfLen - 1] == '\r')) rfLen--;
        if (rfLen > 0) {
            rxFrame[12 + rfLen] = '\0';
            logMsg('R', "(%d B from %02X%02X) %.150s",
                   (int)rfLen, src.addr64[6], src.addr64[7], rfData);
            if (rxCallback) rxCallback(rfData, rfLen, src);
        }
    } else if (ft == XBEE_TX_STATUS && rxFrameLen >= 7) {
        uint8_t deliveryStatus = rxFrame[5];
        if (deliveryStatus == 0x00) {
            stats.txStatusOK++;
            logMsg('S', "TX OK fid=%d", rxFrame[1]);
        } else {
            stats.txStatusFail++;
            const char* reason = "unknown";
            switch (deliveryStatus) {
                case 0x24: reason = "No ACK (no remote)"; break;
                case 0x25: reason = "Network ACK fail"; break;
                case 0x74: reason = "PAYLOAD TOO LARGE!"; break;
                case 0x75: reason = "Indirect msg timeout"; break;
            }
            logMsg('E', "TX FAIL fid=%d err=0x%02X (%s)", rxFrame[1], deliveryStatus, reason);
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

// ── RX processing ───────────────────────────────────────────

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
    diag["oversized_drops"]  = stats.oversizedDrops;
    diag["modem_status_count"] = stats.modemStatusCount;
    diag["last_modem_status"]  = stats.lastModemStatus;
    diag["uptime_ms"]        = millis();
    diag["tx_pin"]           = XBEE_TX_PIN;
    diag["rx_pin"]           = XBEE_RX_PIN;
    diag["baud"]             = XBEE_BAUD;
    diag["broadcast_max"]    = XBEE_BROADCAST_MAX;
    diag["last_source_valid"] = lastSource.valid;
    if (lastSource.valid) {
        char addrStr[20];
        snprintf(addrStr, sizeof(addrStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 lastSource.addr64[0], lastSource.addr64[1],
                 lastSource.addr64[2], lastSource.addr64[3],
                 lastSource.addr64[4], lastSource.addr64[5],
                 lastSource.addr64[6], lastSource.addr64[7]);
        diag["last_source_addr64"] = String(addrStr);
        diag["last_source_addr16"] = lastSource.addr16;
    }

    if (stats.oversizedDrops > 0)
        diag["assessment"] = "OVERSIZED PAYLOADS DETECTED — messages too large for broadcast!";
    else if (stats.rxFramesParsed > 0)
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
