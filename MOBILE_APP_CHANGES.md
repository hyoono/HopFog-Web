# HopFogMobile — Required Changes for ESP32 Compatibility

This document lists the changes needed in the [HopFogMobile](https://github.com/MasterRoxy/HopFogMobile) Android app to work with the ESP32 firmware in this repository. It is designed to be used as a task list for GitHub Copilot.

## Summary

The ESP32 firmware now implements **all 11 API endpoints** that the mobile app calls, at the **exact paths and response formats** the app expects. Most features work without any Android code changes. The changes below are for robustness, WiFi handling, and minor field alignment.

---

## API Compatibility Matrix (No Android Changes Needed)

| # | Mobile App Calls | ESP32 Implements | Status |
|---|---|---|---|
| 1 | `POST /login` (JSON: username, password) | ✅ Returns `{success, user: {user_id, username, email, has_agreed_sos}}` | Works |
| 2 | `GET /status` | ✅ Returns `{"online": true}` | Works |
| 3 | `GET /conversations?user_id=X` | ✅ Returns `[{conversation_id, contact_name, last_message, timestamp}]` | Works |
| 4 | `GET /messages?conversation_id=X&user_id=Y` | ✅ Returns `[{message_id, message_text, sent_at, sender_id, is_from_current_user, sender_username}]` | Works |
| 5 | `POST /send` (JSON: conversation_id, sender_id, message_text) | ✅ Returns `{success, message, secondsRemaining}` | Works |
| 6 | `GET /users?user_id=X` | ✅ Returns `[{id, username}]` excluding self | Works |
| 7 | `POST /create-chat` (JSON: user1_id, user2_id) | ✅ Returns `{conversation_id, contact_name}` | Works |
| 8 | `POST /sos` (JSON: user_id) | ✅ Returns `{conversation_id, contact_name}` | Works |
| 9 | `GET /new-messages?last_id=X&user_id=Y` | ✅ Returns `[Message]` | Works |
| 10 | `POST /agree-sos` (JSON: user_id) | ✅ Returns `{success, message}` | Works |
| 11 | `POST /change-password` (JSON: user_id, old_password, new_password) | ✅ Returns `{success, message}` | Works |

---

## Required Android Changes

### 1. Change BASE_URL to `http://hopfog.com` (High Priority)

**File:** `NetworkManager.kt`

**Task:** Change the `BASE_URL` from `http://192.168.4.1` to `http://hopfog.com`.

The ESP32 runs a captive-portal DNS server that resolves **all** domains (including `hopfog.com`) to its own IP address. Using a domain name instead of a hardcoded IP makes the app more reliable and adaptive — if the ESP32 AP IP ever changes, the domain will still resolve correctly.

**What to do:**
- Change `private const val BASE_URL = "http://192.168.4.1"` to `private const val BASE_URL = "http://hopfog.com"`
- No other changes needed — all API calls already use `BASE_URL` as the prefix

**Copilot prompt:**
> In NetworkManager.kt, change the BASE_URL constant from "http://192.168.4.1" to "http://hopfog.com". The ESP32 runs a captive-portal DNS that resolves hopfog.com to its own IP automatically when connected to the HopFog-Network WiFi.

---

### 2. WiFi Auto-Connection (High Priority)

**File:** `NetworkManager.kt` or create a new `WifiManager.kt`

**Task:** Add automatic connection to the ESP32's WiFi access point when the app starts.

```
WiFi SSID:     "HopFog-Network"
WiFi Password: "changeme123"
Domain:        hopfog.com (resolved by ESP32's captive-portal DNS to 192.168.4.1)
```

**What to do:**
- Before any API call, check if the device is connected to "HopFog-Network"
- If not connected, prompt the user to connect (or auto-connect using `WifiNetworkSpecifier` on Android 10+)
- Show a clear error message if not on the correct WiFi network
- The `GET /status` endpoint (`http://hopfog.com/status`) can be used to verify connectivity

**Copilot prompt:**
> In NetworkManager.kt, add a function `ensureWifiConnection()` that checks if the device is connected to the WiFi SSID "HopFog-Network". If not, show a dialog prompting the user to connect. Use Android's WifiManager to check the current SSID. Call this function before `checkStatus()`.

---

### 3. Handle Offline/Connection Errors Gracefully (Medium Priority)

**File:** `NetworkManager.kt`

**Task:** The ESP32 is a local device that may not always be powered on. All API calls should handle `ConnectException` and `SocketTimeoutException` gracefully.

**What to do:**
- Wrap all Ktor HTTP calls in a try-catch that specifically handles network errors
- Show a user-friendly "Cannot reach HopFog server. Make sure you're connected to HopFog-Network WiFi." message
- The `GET /status` health check should be called periodically (already implemented with 10s polling) — if it fails, show an offline indicator

**Copilot prompt:**
> In NetworkManager.kt, wrap each HTTP call in a try-catch block. Catch `java.net.ConnectException` and `io.ktor.client.network.sockets.ConnectTimeoutException`. When caught, return a result object with `success=false` and `message="Cannot reach HopFog server"`. Update the UI to show an offline banner when status check fails.

---

### 4. User Registration Flow (Medium Priority)

**File:** Create a new `RegisterPage.kt` or add to existing auth flow

**Task:** The mobile app currently only has a login screen. Users need to be created by the admin via the web dashboard first. Consider adding either:
- A note on the login screen: "Ask your admin to create your account"
- Or a self-registration endpoint (would need ESP32 firmware update too)

**Current behavior:** Admin creates mobile users via the web dashboard at `/users` → "Create Mobile User" button. The mobile app then logs in with those credentials.

**Copilot prompt:**
> In the LoginPage, add a small text below the login form that says "Don't have an account? Ask your community admin to create one for you." Style it as a muted helper text.

---

### 5. Timestamp Display Format (Low Priority)

**File:** `ChatModels.kt` or wherever timestamps are displayed

**Task:** The ESP32 returns timestamps as Unix epoch seconds (e.g., `"1708905600"`), not ISO 8601 strings. The app should handle both formats.

**What to do:**
- When parsing `sent_at` or `timestamp` fields, check if the value is a numeric string
- If numeric, convert from epoch seconds to a display format
- If already a date string, parse as before

**Copilot prompt:**
> In the message display logic, add a utility function `formatTimestamp(value: String): String` that checks if the input is a numeric epoch timestamp (all digits). If so, convert it using `java.time.Instant.ofEpochSecond(value.toLong())` and format to "hh:mm a" for today or "MMM dd, hh:mm a" for older dates. If the input is already a date string, parse it as-is.

---

### 6. SOS Message Alert via XBee (Low Priority — Future Enhancement)

**File:** Would need new functionality

**Task:** When an SOS is sent from the mobile app (`POST /sos`), the ESP32 creates a conversation but doesn't automatically send an XBee broadcast. If you want SOS messages from the mobile app to trigger XBee broadcasts to other nodes:

**Option A (no Android change):** After `POST /sos`, have the admin use the web dashboard to escalate the SOS to a broadcast.

**Option B (Android change):** After `POST /sos` succeeds, also call `POST /send` with a message like "SOS Emergency!" in the SOS conversation, then the ESP32 could detect SOS conversations and auto-broadcast.

---

## Testing Checklist

After making changes, test the following flow:

1. [ ] Connect phone to "HopFog-Network" WiFi
2. [ ] App shows online status (GET /status returns `{"online": true}`)
3. [ ] Login with a mobile user account (created via admin dashboard)
4. [ ] Conversation list loads (GET /conversations)
5. [ ] Can send a message (POST /send)
6. [ ] Messages appear in real-time (GET /new-messages polling)
7. [ ] Can create a new chat with another user (POST /create-chat)
8. [ ] SOS button creates conversation with admin (POST /sos)
9. [ ] SOS agreement flow works (POST /agree-sos)
10. [ ] Change password works (POST /change-password)
11. [ ] App handles ESP32 being off gracefully (offline indicator)

## ESP32 Admin Setup (Prerequisites)

Before the mobile app can be used, the admin must:

1. Flash the ESP32 firmware from this repository
2. Copy the `data/sd/www/` files to the SD card
3. Power on the ESP32 — it creates the "HopFog-Network" WiFi
4. Connect to the WiFi from a laptop and open `http://hopfog.com` (the ESP32 captive portal DNS resolves all domains to its own IP)
5. Register an admin account
6. Create mobile user accounts via the Users page → "Create Mobile User"
7. Give the mobile users their credentials
