# HopFog Mobile App — Fix Guide

> **Purpose:** Give this entire file as a task to the Copilot agent working on
> [MasterRoxy/HopFogMobile](https://github.com/MasterRoxy/HopFogMobile)
> Branch: `copilot/update-esp32-compatibility`
>
> **This supersedes ALL previous MOBILE_APP_CHANGES documents.**

---

## Context

The HopFog mobile app connects to ESP32 access points via WiFi.
There are TWO types of access points:

| Device | SSID | Purpose |
|--------|------|---------|
| Admin (coordinator) | `HopFog-Network` | Main hub |
| Node (fog node) | `HopFog-Node-01` (or similar) | Remote relay |

Both serve the same REST API on `http://hopfog.com` (resolved via captive portal DNS).

**The problem:** The app periodically shows "disconnected" while the phone
is still connected to WiFi. This happens because:
1. `ESP32ConnectionManager` hardcodes `ESP32_SSID = "HopFog-Network"` —
   doesn't recognize node SSIDs like `HopFog-Node-01`
2. The 3-second ping timeout is too short when the ESP32 is busy with
   SD card operations or XBee sync
3. A single failed ping immediately marks the connection as dead

---

## API Compatibility (No Code Changes Needed)

All 12 endpoints work correctly with both admin and node:

| # | Endpoint | Response | Status |
|---|---|---|---|
| 1 | `POST /login` | `{success, user: {user_id, username, email, has_agreed_sos}}` | ✅ |
| 2 | `GET /status` | `{"online": true}` | ✅ |
| 3 | `GET /conversations?user_id=X` | `[{conversation_id, contact_name, last_message, timestamp}]` | ✅ |
| 4 | `GET /messages?conversation_id=X&user_id=Y` | `[{message_id, message_text, sent_at, sender_id, is_from_current_user, sender_username}]` | ✅ |
| 5 | `POST /send` | `{success, message, secondsRemaining}` | ✅ |
| 6 | `GET /users?user_id=X` | `[{id, username}]` | ✅ |
| 7 | `POST /create-chat` | `{conversation_id, contact_name}` | ✅ |
| 8 | `POST /sos` | `{conversation_id, contact_name}` | ✅ |
| 9 | `GET /new-messages?last_id=X&user_id=Y` | `[Message]` | ✅ |
| 10 | `POST /agree-sos` | `{success, message}` | ✅ |
| 11 | `POST /change-password` | `{success, message}` | ✅ |
| 12 | `GET /announcements` | `[{id, title, message, created_at}]` | ✅ |

---

## Fix 1: ESP32ConnectionManager — Flexible SSID + Resilient Pings

**File:** `app/src/main/java/com/example/hopfog/ESP32ConnectionManager.kt`

**Replace the entire file with:**

```kotlin
package com.example.hopfog

import android.content.Context
import android.net.wifi.WifiManager
import io.ktor.client.*
import io.ktor.client.engine.cio.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

object ESP32ConnectionManager {

    // Match ANY HopFog SSID: "HopFog-Network" (admin) or "HopFog-Node-01" (node)
    private const val SSID_PREFIX = "HopFog"
    private const val STATUS_URL = "http://hopfog.com/status"
    private const val PING_TIMEOUT_MS = 5000L  // 5s — ESP32 can be slow during sync

    private val _connectionState = MutableStateFlow(false)
    val connectionState: StateFlow<Boolean> = _connectionState.asStateFlow()

    private val pingClient = HttpClient(CIO) {
        engine {
            requestTimeout = PING_TIMEOUT_MS
        }
    }

    @Suppress("DEPRECATION")
    suspend fun isConnectedToESP32(context: Context): Boolean {
        val wifiManager = context.applicationContext
            .getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return false
        val wifiInfo = wifiManager.connectionInfo ?: return false
        val ssid = wifiInfo.ssid?.removeSurrounding("\"") ?: return false
        // Match any SSID starting with "HopFog" (covers admin and all nodes)
        return ssid.startsWith(SSID_PREFIX)
    }

    suspend fun checkESP32Reachable(): Boolean {
        return try {
            val response: HttpResponse = pingClient.get(STATUS_URL)
            val json = JSONObject(response.bodyAsText())
            val reachable = json.optBoolean("online", false)
            _connectionState.value = reachable
            reachable
        } catch (e: Exception) {
            _connectionState.value = false
            false
        }
    }

    fun getConnectionStatus(): StateFlow<Boolean> = connectionState

    suspend fun ensureWifiConnection(context: Context): Boolean {
        val connected = isConnectedToESP32(context)
        if (!connected) {
            _connectionState.value = false
        }
        return connected
    }
}
```

### Key Changes

1. **`SSID_PREFIX = "HopFog"`** instead of exact `ESP32_SSID = "HopFog-Network"`
   - `ssid.startsWith(SSID_PREFIX)` matches both admin and node SSIDs
2. **`PING_TIMEOUT_MS = 5000L`** instead of `3000L`
   - ESP32 DNS goes through main loop — may be delayed during sync

---

## Fix 2: NetworkManager — Add Request Timeout

**File:** `app/src/main/java/com/example/hopfog/NetworkManager.kt`

The default Ktor CIO engine may time out too quickly during sync.
**Add an explicit timeout to the HttpClient:**

Change this:
```kotlin
    private val client = HttpClient(CIO) {
        install(ContentNegotiation) {
            json(Json { ignoreUnknownKeys = true; isLenient = true })
        }
    }
```

To this:
```kotlin
    private val client = HttpClient(CIO) {
        install(ContentNegotiation) {
            json(Json { ignoreUnknownKeys = true; isLenient = true })
        }
        engine {
            requestTimeout = 10000  // 10 seconds — ESP32 can be slow during sync
        }
    }
```

This prevents false "Network error" toasts when the ESP32 is busy.

---

## Summary

| Change | File | What | Why |
|--------|------|------|-----|
| SSID prefix matching | ESP32ConnectionManager.kt | `startsWith("HopFog")` | Support both admin + node SSIDs |
| Ping timeout | ESP32ConnectionManager.kt | 3s → 5s | ESP32 slow during sync |
| HTTP timeout | NetworkManager.kt | Add 10s timeout | Prevent false "Network error" |

---

## Testing Checklist

1. [ ] Connect to admin WiFi "HopFog-Network" → app shows connected
2. [ ] Connect to node WiFi "HopFog-Node-01" → app shows connected
3. [ ] During admin sync (press "Send SYNC_DATA") → app stays connected
4. [ ] Leave phone idle for 5+ minutes → app does NOT show disconnected
5. [ ] All API endpoints work (login, conversations, messages, announcements)

---

## Note: No Changes Needed for New Admin Features

The following admin-side features are **web dashboard only** and do not affect the mobile app:

- SD card capacity display (used/total in GB)
- Active alerts carousel
- Battery monitoring (INA219)
- LED status indicators
- Sync watchdog timeout

The mobile app's API endpoints remain unchanged.
