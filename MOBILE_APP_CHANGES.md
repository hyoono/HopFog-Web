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

All 13 endpoints work correctly with both admin and node:

| # | Endpoint | Response | Status |
|---|---|---|---|
| 1 | `POST /login` | `{success, user: {user_id, username, email, has_agreed_sos}}` | ✅ |
| 2 | `GET /status` | `{"online": true}` | ✅ |
| 3 | `GET /conversations?user_id=X` | `[{conversation_id, contact_name, last_message, timestamp, other_user_id}]` | ✅ UPDATED |
| 4 | `GET /messages?conversation_id=X&user_id=Y` | `[{message_id, message_text, sent_at, sender_id, is_from_current_user, sender_username}]` | ✅ |
| 5 | `POST /send` | `{success, message, secondsRemaining}` | ✅ |
| 6 | `GET /users?user_id=X` | `[{id, username, role, is_online}]` | ✅ UPDATED |
| 7 | `POST /create-chat` | `{conversation_id, contact_name}` | ✅ |
| 8 | `POST /sos` | `{conversation_id, contact_name}` | ✅ |
| 9 | `GET /new-messages?last_id=X&user_id=Y` | `[Message]` | ✅ |
| 10 | `POST /agree-sos` | `{success, message}` | ✅ |
| 11 | `POST /change-password` | `{success, message}` | ✅ |
| 12 | `GET /announcements` | `[{id, title, message, created_at}]` | ✅ |
| **13** | **`GET /api/conversation/{other_user_id}?user_id=X`** | **`[{id, sender_id, receiver_id, content, sent_at}]`** | **✅ NEW** |
| **14** | **`GET /api/users`** | **`[{id, username, email, role, is_active, is_online, created_at}]`** | **✅ UPDATED** |

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

The mobile app's existing API endpoints remain unchanged.

---

## Fix 3: Local Conversation Archiving (Room DB)

> **Objective:** Store conversations locally on the phone for instant loading and offline access.

### New Endpoint: `GET /api/conversation/{other_user_id}?user_id=X`

**Description:** Returns the complete message history between `user_id` and `other_user_id`, sorted oldest → newest.

**Use case:** The mobile app calls this endpoint once per conversation to seed the local SQLite database, then uses `/new-messages?last_id=X&user_id=Y` for incremental updates.

**Request:**
```
GET /api/conversation/3?user_id=1
```

**Response:**
```json
[
  {
    "id": 10,
    "sender_id": 1,
    "receiver_id": 3,
    "content": "Hello!",
    "sent_at": 1677610000
  },
  {
    "id": 11,
    "sender_id": 3,
    "receiver_id": 1,
    "content": "Hi there! How are you?",
    "sent_at": 1677610060
  }
]
```

**Edge cases:**
- No conversation exists → returns empty array `[]`
- Invalid user IDs → returns `400`
- Returns max ~512 messages per conversation (ESP32 memory limit)

---

### Step 1: Add Room Dependencies

In `app/build.gradle.kts`:

```kotlin
plugins {
    // ... existing plugins
    id("com.google.devtools.ksp") version "..." // Match your Kotlin compiler version
}

dependencies {
    val room_version = "2.6.1" // Check https://developer.android.com/jetpack/androidx/releases/room for latest

    implementation("androidx.room:room-runtime:$room_version")
    ksp("androidx.room:room-compiler:$room_version")
    implementation("androidx.room:room-ktx:$room_version")

    // ... existing dependencies
}
```

---

### Step 2: Create Message Entity

**File:** `app/src/main/java/com/example/hopfog/data/MessageEntity.kt`

```kotlin
package com.example.hopfog.data

import androidx.room.Entity
import androidx.room.PrimaryKey
import com.google.gson.annotations.SerializedName

@Entity(tableName = "messages")
data class MessageEntity(
    @PrimaryKey
    val id: Int,

    @SerializedName("sender_id")
    val senderId: Int,

    @SerializedName("receiver_id")
    val receiverId: Int,

    val content: String,

    @SerializedName("sent_at")
    val sentAt: Long
)
```

---

### Step 3: Create DAO

**File:** `app/src/main/java/com/example/hopfog/data/MessageDao.kt`

```kotlin
package com.example.hopfog.data

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import kotlinx.coroutines.flow.Flow

@Dao
interface MessageDao {
    @Query("""
        SELECT * FROM messages
        WHERE (senderId = :userId AND receiverId = :otherUserId)
           OR (senderId = :otherUserId AND receiverId = :userId)
        ORDER BY sentAt ASC
    """)
    fun getConversation(userId: Int, otherUserId: Int): Flow<List<MessageEntity>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAll(messages: List<MessageEntity>)

    @Query("SELECT MAX(id) FROM messages WHERE (senderId = :userId AND receiverId = :otherUserId) OR (senderId = :otherUserId AND receiverId = :userId)")
    suspend fun getMaxMessageId(userId: Int, otherUserId: Int): Int?

    @Query("DELETE FROM messages WHERE (senderId = :userId AND receiverId = :otherUserId) OR (senderId = :otherUserId AND receiverId = :userId)")
    suspend fun deleteConversation(userId: Int, otherUserId: Int)
}
```

---

### Step 4: Create Database

**File:** `app/src/main/java/com/example/hopfog/data/AppDatabase.kt`

```kotlin
package com.example.hopfog.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

@Database(entities = [MessageEntity::class], version = 1, exportSchema = false)
abstract class AppDatabase : RoomDatabase() {
    abstract fun messageDao(): MessageDao

    companion object {
        @Volatile
        private var INSTANCE: AppDatabase? = null

        fun getDatabase(context: Context): AppDatabase {
            return INSTANCE ?: synchronized(this) {
                val instance = Room.databaseBuilder(
                    context.applicationContext,
                    AppDatabase::class.java,
                    "hopfog_messages"
                ).build()
                INSTANCE = instance
                instance
            }
        }
    }
}
```

---

### Step 5: Create Repository

**File:** `app/src/main/java/com/example/hopfog/data/ChatRepository.kt`

```kotlin
package com.example.hopfog.data

import kotlinx.coroutines.flow.Flow

class ChatRepository(
    private val dao: MessageDao,
    private val networkManager: NetworkManager // your existing NetworkManager
) {
    fun getConversation(userId: Int, otherUserId: Int): Flow<List<MessageEntity>> =
        dao.getConversation(userId, otherUserId)

    suspend fun syncConversation(userId: Int, otherUserId: Int) {
        try {
            val response = networkManager.getConversationHistory(otherUserId, userId)
            if (response.isNotEmpty()) {
                dao.insertAll(response)
            }
        } catch (e: Exception) {
            // Network error — fall back to cached data
        }
    }
}
```

---

### Step 6: Add Network Call to NetworkManager

Add this method to your existing `NetworkManager.kt`:

```kotlin
suspend fun getConversationHistory(otherUserId: Int, userId: Int): List<MessageEntity> {
    val response = client.get("$BASE_URL/api/conversation/$otherUserId") {
        parameter("user_id", userId)
    }
    return response.body()
}
```

---

### Step 7: Use in ViewModel

```kotlin
class ChatViewModel(
    private val repository: ChatRepository,
    private val userId: Int,
    private val otherUserId: Int
) : ViewModel() {

    val messages: Flow<List<MessageEntity>> =
        repository.getConversation(userId, otherUserId)

    init {
        // Sync from server on first load
        viewModelScope.launch {
            repository.syncConversation(userId, otherUserId)
        }
    }
}
```

---

### Sync Strategy

| Scenario | Action |
|----------|--------|
| **First open** | Call `/api/conversation/{other_user_id}?user_id=X` → insert all into Room |
| **Subsequent opens** | Load from Room instantly (offline-first), then sync in background |
| **New messages** | Use `/new-messages?last_id=X&user_id=Y` to get only new messages, insert into Room |
| **Send message** | POST to `/send`, then insert locally into Room immediately (optimistic) |

This gives the user instant message loading with offline access.

---

## Fix 14: Correct DM / SOS Message Tagging

**Problem:** When a user sends an SOS and then sends a follow-up DM to admin, the conversation retains the DM status instead of maintaining SOS context. Vice versa: when a DM user sends an SOS, it may still show as DM.

**Solution:** The mobile app should tag each message with a `msg_type` field:

- When sending from SOS mode: set `msg_type: "sos_request"` on the message payload
- When sending from regular chat: set `msg_type: "message"` on the message payload
- The server `POST /api/resident-admin/send` endpoint already accepts a `kind` field (`message` or `sos_request`)

**Android Implementation:**
```kotlin
// In your message sending function, pass the correct kind:
val kind = if (isSOSMode) "sos_request" else "message"

val formBody = FormBody.Builder()
    .add("body", messageText)
    .add("kind", kind)  // <-- This determines DM vs SOS
    .build()
```

The server will use this `kind` field to properly categorize the message in the SOS Console vs regular DM inbox.

---

## Fix 15: Send Authorization Header with API Requests

**Problem:** Mobile users don't appear as "online" in the web admin's Users page because the app doesn't send its auth token with subsequent API requests after login.

**Root Cause:** The server now tracks active sessions and exposes `GET /api/users/online` which returns user IDs with active session tokens. The app receives an `access_token` from `POST /login` but may not be sending it in the `Authorization` header for subsequent requests.

**Solution:** After login, store the `access_token` and send it as `Authorization: Bearer <token>` in ALL subsequent API requests.

**Android Implementation:**

```kotlin
// After login response:
val accessToken = jsonResponse.getString("access_token")
// Store it (e.g., SharedPreferences or in-memory singleton)

// For ALL subsequent requests, add the header:
val request = Request.Builder()
    .url("http://hopfog.com/api/users")
    .addHeader("Authorization", "Bearer $accessToken")
    .build()
```

**Reference Repository:** https://github.com/christian-dela-cruz/HopFogMobile (branch: copilot/add-local-conversation-archiving)

This ensures that:
1. Mobile users appear as "online" (green dot) in the admin Users page
2. The admin can see which mobile users are currently active
3. API calls that require authentication will succeed

---

## Fix 16: SOS Conversation Mode Switching

**Problem:** Once a conversation is created in SOS mode (`POST /sos`), ALL subsequent messages in that conversation inherit the SOS flag. Similarly, a DM conversation always stays as DM. Users can't switch modes within the same admin conversation.

**Root Cause:** The server uses a per-conversation `is_sos` flag (in `conversations.json`), not a per-message type field.

**Solution:** When the user triggers SOS:
1. The app should call `POST /sos` (which creates/finds a conversation with `is_sos=1`)
2. The server now ALSO writes to `resident_admin_msgs.json` (which feeds the SOS Console)
3. For DM messages to admin, use `POST /create-chat` (which creates with `is_sos=0`)

The user effectively has two "conversations" with admin — one for SOS, one for DM. This is the intended behavior.

**Android Implementation:**

```kotlin
// When user triggers SOS:
fun triggerSOS(userId: Int) {
    val body = FormBody.Builder()
        .add("user_id", userId.toString())
        .build()
    // POST /sos → returns {conversation_id, contact_name}
    // Use this conversation_id for SOS messages
}

// When user wants regular DM to admin:
fun startDMWithAdmin(userId: Int, adminId: Int) {
    val body = FormBody.Builder()
        .add("user1_id", userId.toString())
        .add("user2_id", adminId.toString())
        .build()
    // POST /create-chat → returns {conversation_id, contact_name}
    // Use this conversation_id for regular DM messages
}
```

**Reference Repository:** https://github.com/christian-dela-cruz/HopFogMobile

---

## Fix 17: Disable Regular DM to Admin — SOS-Only Admin Contact

**Problem:** When a resident triggers SOS and then continues messaging the admin through regular DM, the message is still tagged as SOS (or vice versa). The per-conversation `is_sos` flag makes it impossible to reliably distinguish individual messages.

**Solution:** Remove the ability to send regular DMs to the admin account. All admin contact should go through the SOS function exclusively. This simplifies the UX and eliminates the DM/SOS confusion.

**Android Implementation:**

1. **Hide admin from the user list / new chat screen:**

```kotlin
// In your user list adapter or when fetching users for new chat:
// Filter out admin accounts so residents can't start a regular DM with admin
val chatableUsers = allUsers.filter { it.role != "admin" }
```

2. **Block DM to admin in the send function:**

```kotlin
// In your message sending function, check if the recipient is admin:
fun sendMessage(conversationId: Int, recipientId: Int, messageText: String, userId: Int) {
    // If the conversation is with admin, redirect to SOS flow
    if (isAdminConversation(conversationId)) {
        // Show a dialog: "To contact the admin, please use the SOS function"
        showSOSRedirectDialog()
        return
    }
    // ... normal send logic ...
}
```

3. **Show SOS as the only way to reach admin in the UI:**

```kotlin
// In your contact list or conversation list:
// If there's an admin conversation, show it with SOS branding only
// Remove any "Message Admin" or "DM Admin" button
// Keep only "SOS" button for admin contact
```

**Result:**
- Residents can ONLY contact admin through the SOS function (`POST /sos`)
- Regular DMs between residents still work normally (`POST /create-chat` + `POST /send`)
- The admin sees ALL resident-initiated messages in the SOS Console (as intended)
- No more confusion between DM and SOS tagging

---

## Fix 18: Show Online Users in "New Messages" Screen

**Problem:** The "New Messages" screen (where users select a contact to start a conversation) shows ALL registered users, regardless of whether they're online or offline. Users should only see who is currently available.

**Server Change:** `GET /api/users` now includes an `is_online` field (boolean) for each user. This is set based on active session tokens — no need for a separate API call.

**Example Response:**
```json
[
  {"id": 1, "username": "admin", "email": "admin@local", "role": "admin", "is_active": 1, "is_online": true},
  {"id": 2, "username": "john", "email": "john@local", "role": "mobile", "is_active": 1, "is_online": false}
]
```

**Android Implementation:**

**File:** `app/src/main/java/com/example/hopfog/NewMessageActivity.kt` (or wherever the New Messages screen is)

1. **Update the User data class to include `is_online`:**

```kotlin
data class User(
    val id: Int,
    val username: String,
    val email: String,
    val role: String,
    @SerializedName("is_active")
    val isActive: Int,
    @SerializedName("is_online")
    val isOnline: Boolean = false
)
```

2. **Filter users: show online only, exclude admin, exclude self:**

```kotlin
// When fetching users from GET /api/users:
val allUsers: List<User> = parseUsersFromResponse(responseBody)

// Filter: only online, non-admin, active users (excluding self)
val chatableUsers = allUsers.filter { user ->
    user.isOnline &&           // Only show online users
    user.role != "admin" &&    // Exclude admin accounts (use SOS instead)
    user.isActive == 1 &&      // Only active accounts
    user.id != currentUserId   // Exclude self
}
```

3. **Show online indicator in the user list adapter:**

```kotlin
// In your RecyclerView adapter for user list items:
fun bindUser(user: User) {
    usernameTextView.text = user.username
    // Show green dot for online users
    onlineDotView.visibility = if (user.isOnline) View.VISIBLE else View.GONE
}
```

4. **Add a toggle to show all users vs. online only:**

```kotlin
// Optional: Add a switch/toggle in the toolbar
var showOnlineOnly = true

fun refreshUserList() {
    val filtered = if (showOnlineOnly) {
        allUsers.filter { it.isOnline && it.role != "admin" && it.id != currentUserId }
    } else {
        allUsers.filter { it.role != "admin" && it.id != currentUserId }
    }
    adapter.submitList(filtered)
}
```

**Result:**
- New Messages screen shows only online, non-admin users by default
- Users can optionally toggle to see all users
- Green dot indicates who is online

---

## Fix 19: Filter Viewable Users in "New Messages" Screen

**Problem:** Users need the ability to select which users are viewable — e.g., only online users, or a custom selection.

**Android Implementation:**

**File:** `app/src/main/java/com/example/hopfog/NewMessageActivity.kt`

1. **Add filter chips or a filter menu:**

```kotlin
// In your activity/fragment layout, add filter options:
// - "Online Only" (default ON)
// - "All Users"
// - Search by name

enum class UserFilter {
    ONLINE_ONLY,
    ALL_USERS
}

private var currentFilter = UserFilter.ONLINE_ONLY
private var searchQuery = ""

fun applyFilters(users: List<User>): List<User> {
    return users.filter { user ->
        // Always exclude admin accounts and self
        user.role != "admin" && user.id != currentUserId &&
        // Apply online filter
        (currentFilter == UserFilter.ALL_USERS || user.isOnline) &&
        // Apply search filter
        (searchQuery.isEmpty() || user.username.contains(searchQuery, ignoreCase = true))
    }
}
```

2. **Add search bar to filter by username:**

```kotlin
// In your toolbar or layout:
searchEditText.addTextChangedListener(object : TextWatcher {
    override fun afterTextChanged(s: Editable?) {
        searchQuery = s?.toString() ?: ""
        adapter.submitList(applyFilters(allUsers))
    }
    // ... other overrides
})
```

**Result:**
- Users can filter the contact list by online status and search by name
- Admin accounts are always hidden from the contact list

---

## Fix 20: Admin Accounts Hidden Server-Side (No App Changes Needed)

**Problem:** Admin accounts should NOT appear in the "New Messages" or "Chats" screens. Residents should contact admin ONLY through the SOS function.

**Server-side fix (already applied):** The server now filters admin accounts out at the API level:
- `GET /users?user_id=X` — admin users are **excluded** from the response. The `is_admin` field has been removed.
- `GET /conversations?user_id=X` — conversations with admin accounts are **excluded** from the response. The `is_admin` field has been removed.

**What the mobile app should do:**
1. **Remove any `is_admin` filtering logic** — the server already excludes admins
2. **Remove the `is_admin` field** from User and Conversation data classes (or keep it with a default for backward compat)
3. Display all users returned by `/users` — they are guaranteed to be non-admin
4. Display all conversations returned by `/conversations` — they are guaranteed to be non-admin

### Simplified User model (remove is_admin):

```kotlin
@Serializable
data class SelectableUser(
    val id: Int,
    val username: String,
    val role: String = "mobile",
    @SerialName("is_online") val isOnline: Boolean = false
)
```

### Simplified Conversation model (remove is_admin):

```kotlin
@Serializable
data class ChatConversation(
    @SerialName("conversation_id") val conversationId: Int,
    @SerialName("contact_name") val contactName: String,
    @SerialName("last_message") val lastMessage: String? = null,
    @SerialName("timestamp") val timestamp: String? = null,
    @SerialName("other_user_id") val otherUserId: Int = 0
)
```

**Result:**
- Admin accounts are completely hidden from "New Messages" and "Chats" (server-side)
- The ONLY way to contact admin is through the SOS function
- No client-side filtering needed

**Reference Repository:** https://github.com/christian-dela-cruz/HopFogMobile (branch: copilot/add-local-conversation-archiving)

---

## Fix 21: Token Restoration + Auto Re-login on 401

**Problem:** After the ESP32 restarts, all in-memory sessions are lost. The mobile app has a stale `access_token` saved in SharedPreferences. API calls succeed because mobile endpoints use `user_id` query parameter (not auth tokens), but the user doesn't appear online because `validateToken()` returns -1 for the stale token.

**Root Cause:** The server now tracks user activity via `markUserActive()` on every mobile API call (using the `user_id` param). This means mobile users will appear online within 5 minutes of their last API call, even with stale tokens. **No mobile app changes required for online status tracking.**

However, for security, the app should still re-login when it detects a stale token.

**File:** `app/src/main/java/com/example/hopfog/NetworkManager.kt`

**Add token restoration on app startup** — In the Application class or main activity's `onCreate`:

```kotlin
// Restore access token from saved session
val savedToken = SessionManager.getAccessToken(context)
if (savedToken.isNotEmpty()) {
    NetworkManager.setAccessToken(savedToken)
}
```

**Result:**
- Mobile users now appear as "online" (green dot) in admin Users page — the server tracks activity via `user_id` parameter on every mobile API call
- The 5-minute activity window means users who have used the app within 5 minutes appear online
- Token restoration ensures the `Authorization: Bearer` header is sent on subsequent requests

---

## Fix 22: Server-Side Admin Filtering (Root Cause Fixed)

**Problem:** The server returned all users (including admins) without any filtering.
Admin users appeared in the mobile app's "New Messages" and "Chats" screens.
"Online Only" showed no users because the `is_online` field was missing from the response.

**Root Cause (fixed):** The `GET /users` route had a **duplicate handler** in `web_server.cpp`
that was registered FIRST, overriding the filtered handler in `api_handlers.cpp`.
ESPAsyncWebServer uses first-match routing, so the unfiltered handler always won.
The fix consolidates the mobile API logic into the `web_server.cpp` handler with
admin filtering, `role`, `is_online`, and `markUserActive()` tracking.

### A. `GET /users?user_id=X` — admin users excluded server-side

**Response format:** `[{id, username, role, is_online}]`

- Admin accounts are **never returned** — no client-side filtering needed
- `is_admin` field has been removed (unnecessary since admins are excluded)
- `is_online` field indicates whether the user is currently active (made an API call within 5 min)

**Update the User/SelectableUser model** — remove `is_admin` if you added it:

```kotlin
@Serializable
data class SelectableUser(
    val id: Int,
    val username: String,
    val role: String = "mobile",
    @SerialName("is_online") val isOnline: Boolean = false
)
```

**"Online Only" toggle** — use `is_online` directly:

```kotlin
// When "Online Only" is selected:
val onlineUsers = allUsers.filter { it.isOnline }

// Note: is_online is true when the user has made an API call within the last 5 minutes.
// Users who have the app open and active will be marked online because the app
// periodically calls /users and /conversations, which updates their activity timestamp.
```

### B. `GET /conversations?user_id=X` — admin conversations excluded server-side

**Response format:** `[{conversation_id, contact_name, last_message, timestamp, other_user_id}]`

- Conversations with admin accounts are **never returned**
- `is_admin` field has been removed (unnecessary since admin conversations are excluded)

**Update the `ChatConversation` model** — remove `is_admin` if you added it:

```kotlin
@Serializable
data class ChatConversation(
    @SerialName("conversation_id") val conversationId: Int,
    @SerialName("contact_name") val contactName: String,
    @SerialName("last_message") val lastMessage: String? = null,
    @SerialName("timestamp") val timestamp: String? = null,
    @SerialName("other_user_id") val otherUserId: Int = 0
)
```

### C. No client-side admin filtering needed

**Remove any `!it.isAdmin` filters** from the New Messages and Chats screens.
The server handles this now. The app only needs to handle:
1. "Online Only" toggle using `is_online` field
2. "All Users" shows all returned users (no admin accounts will be present)

### D. Keeping users marked as "online"

The server tracks user activity via API calls. Every time the app calls
`GET /users?user_id=X` or `GET /conversations?user_id=X`, the requesting user
is marked as active for 5 minutes. So as long as the app periodically refreshes
its data (which it does when navigating between screens), the user will appear
online to other users.

**Result:**
- "New Messages" shows only non-admin users (server-filtered)
- "Online Only" toggle works using `is_online` field
- "Chats" shows only non-admin conversations (server-filtered)
- Users contacting admin must use SOS function only
