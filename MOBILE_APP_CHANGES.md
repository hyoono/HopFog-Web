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
| **13** | **`GET /api/conversation/{other_user_id}?user_id=X`** | **`[{id, sender_id, receiver_id, content, sent_at}]`** | **✅ NEW** |

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
