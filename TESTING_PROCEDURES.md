# HopFog Network Testing Procedures

This document provides testing procedures adapted for the HopFog ESP32-CAM + XBee S2C system. All tests use the built-in firmware diagnostics and web dashboard — no code modifications needed.

---

## 1. Prerequisites for All Tests

### Hardware
- **Admin Node**: ESP32-CAM with XBee S2C configured as **Coordinator** (CE=1)
- **Node-01**: ESP32-CAM with XBee S2C configured as **Router** (CE=0)
- Both modules on the same PAN ID (ID), same baud rate (9600)
- Both powered on and running HopFog firmware

### Verification
1. Connect to `HopFog-Network` WiFi
2. Open `http://192.168.4.1/admin/messaging/testing` (XBee Testing page)
3. Verify the serial monitor shows `RX` messages (PONG responses from node)
4. Open the Fog Nodes page and verify the node appears as "Active"

---

## 2. Network Performance Testing

**Objective:** Evaluate packet loss rate and throughput of the XBee ZigBee link.

### How It Works
The admin's XBee Testing page already has "Send Test Packet" functionality. The XBee diagnostics panel shows `txStatusOK`, `txStatusFail`, `txFramesSent`, and `rxFramesParsed` — these are the counters we use.

### Procedure

**Step 1: Reset Counters**
1. Navigate to `http://192.168.4.1/admin/messaging/testing`
2. Note the current values of all counters in the Diagnostics panel
3. Record as baseline: `TX_sent_start`, `TX_ok_start`, `TX_fail_start`, `RX_parsed_start`

**Step 2: Execute Test Run (50 packets)**
1. Use the "Raw Send" text box to type a test message (e.g., `TEST_PKT_001`)
2. Click "Send" 50 times (or use browser console):
   ```javascript
   // Paste this in browser console (F12 → Console) for automated sending
   (async function() {
       for (let i = 0; i < 50; i++) {
           await fetch('/api/xbee/send-raw', {
               method: 'POST',
               headers: {'Content-Type': 'application/json'},
               body: JSON.stringify({data: 'TEST_PKT_' + String(i).padStart(3, '0')})
           });
           await new Promise(r => setTimeout(r, 200)); // 200ms between packets
       }
       console.log('Done! Check diagnostics.');
   })();
   ```
3. Wait 15 seconds for all TX status responses to arrive

**Step 3: Record Results**
1. Click "Refresh Diagnostics" on the testing page
2. Record new counter values: `TX_sent_end`, `TX_ok_end`, `TX_fail_end`, `RX_parsed_end`
3. Calculate:
   - **Packets Sent** = `TX_sent_end - TX_sent_start`
   - **Packets Received (OK)** = `TX_ok_end - TX_ok_start`
   - **Packets Failed** = `TX_fail_end - TX_fail_start`
   - **Packet Loss (%)** = `(Packets Failed / Packets Sent) × 100`

**Step 4: Calculate Throughput**
1. Test payload = `TEST_PKT_XXX` = 12 bytes
2. Total duration ≈ 50 × 200ms = 10 seconds
3. **Throughput (B/s)** = `(Packets Received × 12) / Duration`

**Step 5: Repeat**
Repeat Steps 1–4 for 5 test runs. Calculate averages.

### Results Table

| Run | Sent | OK | Failed | Loss % | Duration (s) | Throughput (B/s) | PASS/FAIL |
|-----|------|----|--------|--------|--------------|------------------|-----------|
| 1   |      |    |        |        |              |                  |           |
| 2   |      |    |        |        |              |                  |           |
| 3   |      |    |        |        |              |                  |           |
| 4   |      |    |        |        |              |                  |           |
| 5   |      |    |        |        |              |                  |           |
| **Avg** |  |    |        |        |              |                  |           |

**Pass Criteria:** Average packet loss < 2%

---

## 3. One-Way Delay Testing

**Objective:** Measure one-way transmission delay using the PING/PONG round-trip method.

### How It Works
The admin broadcasts `{"cmd":"PING","node_id":"admin"}` every 10 seconds. The node responds with `{"cmd":"PONG","node_id":"node-01",...}`. The XBee log on the testing page records timestamps for both TX and RX events.

### Procedure

**Step 1: Prepare**
1. Open the XBee Testing page
2. Clear the serial log view
3. Note the current system uptime (shown in diagnostics)

**Step 2: Capture PING/PONG Timestamps**
Use the browser console to measure round-trip time:
```javascript
// Automated one-way delay measurement (5 runs with varying payload)
(async function() {
    const payloads = [
        'A',                              // 1 byte
        'ABCDEFGHIJ',                     // 10 bytes
        'A'.repeat(30),                   // 30 bytes
        'A'.repeat(50),                   // 50 bytes
        'A'.repeat(70),                   // 70 bytes (near broadcast limit)
    ];
    
    for (let p = 0; p < payloads.length; p++) {
        const payload = payloads[p];
        const t0 = Date.now();  // t₀: departure
        
        const resp = await fetch('/api/xbee/test', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({data: payload})
        });
        
        const t3 = Date.now();  // t₃: arrival of HTTP response
        
        // HTTP response includes internal processing; approximate:
        // RT = t₃ - t₀ (includes ESP32 processing + XBee TX time)
        // Processing Time ≈ 5ms (ESP32 JSON parse + response)
        // One-Way Delay ≈ (RT - 5) / 2
        
        const RT = t3 - t0;
        const PT = 5; // estimated processing time (ms)
        const oneWay = (RT - PT) / 2;
        
        console.log(`Payload ${payload.length}B: RT=${RT}ms, OneWay≈${oneWay.toFixed(1)}ms`);
        
        await new Promise(r => setTimeout(r, 1000));
    }
})();
```

**Step 3: Record Results**

### Results Table

| Payload (bytes) | RT (ms) | PT (ms) | One-Way Delay (ms) | PASS/FAIL |
|-----------------|---------|---------|---------------------|-----------|
| 1               |         |    5    |                     |           |
| 10              |         |    5    |                     |           |
| 30              |         |    5    |                     |           |
| 50              |         |    5    |                     |           |
| 70              |         |    5    |                     |           |
| **Average**     |         |         |                     |           |

**Note:** For more precise measurements, use the XBee serial log timestamps on the Testing page. Each log entry shows `TX → Xs` and `RX ← Xs` with the timestamp relative to boot.

---

## 4. Payload Size and Concurrent Load Testing

### Scenario A: Single-Stream Load

**Objective:** Measure actual message rate at different payload sizes.

**Procedure:**
```javascript
// Single-stream load test — adjust payloadSize and count
async function singleStreamTest(payloadSize, count, ratePerSec) {
    const payload = 'X'.repeat(payloadSize);
    const delayMs = 1000 / ratePerSec;
    let sent = 0, ok = 0, failed = 0;
    
    const startTime = Date.now();
    
    for (let i = 0; i < count; i++) {
        try {
            const resp = await fetch('/api/xbee/send-raw', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({data: payload})
            });
            const result = await resp.json();
            sent++;
            if (result.success) ok++; else failed++;
        } catch(e) { sent++; failed++; }
        
        await new Promise(r => setTimeout(r, delayMs));
    }
    
    const endTime = Date.now();
    const duration = (endTime - startTime) / 1000;
    
    console.log(`=== Single-Stream Results ===`);
    console.log(`Payload: ${payloadSize}B, Count: ${count}`);
    console.log(`Duration: ${duration.toFixed(1)}s`);
    console.log(`Sent: ${sent}, OK: ${ok}, Failed: ${failed}`);
    console.log(`Actual Send Rate: ${(sent/duration).toFixed(1)} msg/s`);
    console.log(`Actual Receive Rate: ${(ok/duration).toFixed(1)} msg/s`);
    console.log(`% Difference: ${(Math.abs(ratePerSec - sent/duration) / ratePerSec * 100).toFixed(1)}%`);
}

// Run tests:
// Small payload (10 bytes, 5 msg/s, 50 messages)
await singleStreamTest(10, 50, 5);

// Medium payload (40 bytes, 5 msg/s, 50 messages)
await singleStreamTest(40, 50, 5);

// Large payload (70 bytes, 5 msg/s, 50 messages)
await singleStreamTest(70, 50, 5);
```

### Results Table — Scenario A

| Payload | Programmed Rate | Messages Sent | Actual Rate (m/s) | % Difference | PASS/FAIL |
|---------|-----------------|---------------|---------------------|--------------|-----------|
| Small (10B)  | 5 m/s    |               |                     |              |           |
| Medium (40B) | 5 m/s    |               |                     |              |           |
| Large (70B)  | 5 m/s    |               |                     |              |           |

---

### Scenario B: Concurrent Load (Two Senders)

**Objective:** Measure aggregate throughput with multiple senders.

**Setup:** This requires **two browser tabs** — one connected to the Admin AP, one to the Node AP. Both send simultaneously to the other.

**Procedure:**
1. Open two browser tabs:
   - Tab 1: `http://192.168.4.1/admin/messaging/testing` (Admin)
   - Tab 2: `http://192.168.4.1/admin/messaging/testing` (Node — if accessible)
2. In each tab's console, run the single-stream test simultaneously
3. On the admin, check diagnostics to see total RX received

**Alternative (single-device test):**
Since the node may not have a testing page, simulate concurrent load from admin by launching two fetch loops in parallel:

```javascript
async function concurrentTest(count, ratePerSec) {
    const delayMs = 1000 / ratePerSec;
    let totalSent = 0;
    
    const startTime = Date.now();
    
    // Two "senders" in parallel
    await Promise.all([
        (async () => {
            for (let i = 0; i < count; i++) {
                await fetch('/api/xbee/send-raw', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({data: 'SENDER_A_' + i})
                });
                totalSent++;
                await new Promise(r => setTimeout(r, delayMs));
            }
        })(),
        (async () => {
            for (let i = 0; i < count; i++) {
                await fetch('/api/xbee/send-raw', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({data: 'SENDER_B_' + i})
                });
                totalSent++;
                await new Promise(r => setTimeout(r, delayMs));
            }
        })()
    ]);
    
    const duration = (Date.now() - startTime) / 1000;
    const expectedRate = ratePerSec * 2;
    const actualRate = totalSent / duration;
    const degradation = ((expectedRate - actualRate) / expectedRate * 100);
    
    console.log(`=== Concurrent Load Results ===`);
    console.log(`Total Sent: ${totalSent}, Duration: ${duration.toFixed(1)}s`);
    console.log(`Expected Rate: ${expectedRate} m/s, Actual: ${actualRate.toFixed(1)} m/s`);
    console.log(`Throughput Degradation: ${degradation.toFixed(1)}%`);
}

// Run 3 times
for (let run = 1; run <= 3; run++) {
    console.log(`\n--- Run ${run} ---`);
    await concurrentTest(25, 5);  // 25 messages per sender, 5 m/s each
    await new Promise(r => setTimeout(r, 2000));
}
```

### Results Table — Scenario B

| Run | Total Sent | Total Received | Duration (s) | Expected Rate | Actual Rate | Degradation % | PASS/FAIL |
|-----|-----------|----------------|--------------|---------------|-------------|---------------|-----------|
| 1   |           |                |              |               |             |               |           |
| 2   |           |                |              |               |             |               |           |
| 3   |           |                |              |               |             |               |           |
| **Avg** |       |                |              |               |             |               |           |

---

## 5. Quick XBee Health Check

For a quick verification that the XBee link is healthy without running full test suites:

1. Open `http://192.168.4.1/admin/messaging/testing`
2. Check the Diagnostics panel:
   - **TX Direction**: Should show `TX_OK > 0` and `TX_FAIL = 0`
   - **RX Direction**: Should show `rxFramesParsed > 0`
   - **Assessment**: Should be `TX ✓` and `RX ✓`
3. Send a test broadcast message and verify TX status returns 0x00 (success)
4. Check the Fog Nodes page — node should be "Active" with heartbeat < 30s ago

## 6. Sync Performance Test

1. Navigate to Fog Nodes page
2. Click the Sync button for a node
3. Monitor the XBee serial log on the Testing page
4. Count `SD` (SYNC_DATA) and `SC` (Sync Continuation) messages
5. Verify `DONE` message appears at the end
6. Verify the node's WiFi AP remains accessible throughout the sync (no disconnections)
7. Check the node's `/announcements` endpoint to verify data was received
