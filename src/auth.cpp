/*
 * auth.cpp — Simple token-based session authentication for ESP32
 *
 * Uses SHA-256 (mbedtls, built into ESP-IDF) for password hashing and
 * hardware RNG for token generation.
 */

#include "auth.h"
#include "config.h"

#include <mbedtls/sha256.h>
#include <esp_random.h>

// ── In-memory session store ─────────────────────────────────────────

struct Session {
    String token;
    int    userId;
    bool   used;
};

static Session sessions[MAX_ACTIVE_TOKENS];

// ── Activity tracker (supplements sessions for mobile users) ────────
// Mobile users may have stale tokens after ESP32 restart but still
// make API calls with user_id query params.  We track their activity
// so they appear online in the admin Users page.
#define ACTIVITY_TIMEOUT_MS (5UL * 60 * 1000)   // 5 minutes
struct UserActivity {
    int            userId;
    unsigned long  lastSeen;   // millis()
    bool           used;
};
static UserActivity activity[MAX_USERS];

void authInit() {
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        sessions[i].used = false;
    }
    for (int i = 0; i < MAX_USERS; i++) {
        activity[i].used = false;
    }
    dbgprintln("[Auth] Initialised");
}

// ── Password hashing ───────────────────────────────────────────────

static String sha256Hex(const String &input) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
    mbedtls_sha256_update(&ctx, (const unsigned char *)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    String hex;
    hex.reserve(64);
    for (int i = 0; i < 32; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        hex += buf;
    }
    return hex;
}

// Generate a random 16-char hex salt
static String generateSalt() {
    String salt;
    salt.reserve(16);
    for (int i = 0; i < 8; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", (uint8_t)esp_random());
        salt += buf;
    }
    return salt;
}

String hashPassword(const String &password) {
    // Per-user random salt stored as "salt:hash"
    String salt = generateSalt();
    String hash = sha256Hex(salt + ":" + password);
    return salt + ":" + hash;
}

bool verifyPassword(const String &password, const String &storedHash) {
    // Stored format: "salt:hash"
    int sep = storedHash.indexOf(':');
    if (sep < 0) return false;
    String salt = storedHash.substring(0, sep);
    String expectedHash = storedHash.substring(sep + 1);
    String computedHash = sha256Hex(salt + ":" + password);
    return computedHash == expectedHash;
}

// ── Token management ────────────────────────────────────────────────

static String generateRandomToken() {
    String token;
    token.reserve(TOKEN_LENGTH);
    for (int i = 0; i < TOKEN_LENGTH / 2; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", (uint8_t)esp_random());
        token += buf;
    }
    return token;
}

String createSessionToken(int userId) {
    // Allow concurrent sessions (web + mobile can both be online)
    // Only evict if table is full (handled below)

    // Find free slot
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        if (!sessions[i].used) {
            sessions[i].token  = generateRandomToken();
            sessions[i].userId = userId;
            sessions[i].used   = true;
            return sessions[i].token;
        }
    }

    // Table full – overwrite oldest (slot 0)
    sessions[0].token  = generateRandomToken();
    sessions[0].userId = userId;
    sessions[0].used   = true;
    return sessions[0].token;
}

int validateToken(const String &token) {
    if (token.isEmpty()) return -1;
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        if (sessions[i].used && sessions[i].token == token) {
            return sessions[i].userId;
        }
    }
    return -1;
}

void removeToken(const String &token) {
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        if (sessions[i].used && sessions[i].token == token) {
            sessions[i].used = false;
            return;
        }
    }
}

String extractTokenFromCookie(const String &cookieHeader) {
    // Cookie: access_token=Bearer <token>; ...
    int start = cookieHeader.indexOf("access_token=Bearer ");
    if (start < 0) return "";
    start += 20; // length of "access_token=Bearer "
    int end = cookieHeader.indexOf(';', start);
    if (end < 0) end = cookieHeader.length();
    return cookieHeader.substring(start, end);
}

int countActiveSessions() {
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        if (sessions[i].used) count++;
    }
    return count;
}

int getActiveUserIds(int *outIds, int maxOut) {
    int count = 0;
    unsigned long now = millis();

    // 1. Token-based sessions (web admin, recent mobile logins)
    for (int i = 0; i < MAX_ACTIVE_TOKENS && count < maxOut; i++) {
        if (!sessions[i].used) continue;
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (outIds[j] == sessions[i].userId) { found = true; break; }
        }
        if (!found) outIds[count++] = sessions[i].userId;
    }

    // 2. Activity-based tracking (mobile users with stale tokens)
    for (int i = 0; i < MAX_USERS && count < maxOut; i++) {
        if (!activity[i].used) continue;
        if ((now - activity[i].lastSeen) > ACTIVITY_TIMEOUT_MS) {
            activity[i].used = false;  // expired
            continue;
        }
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (outIds[j] == activity[i].userId) { found = true; break; }
        }
        if (!found) outIds[count++] = activity[i].userId;
    }

    return count;
}

void markUserActive(int userId) {
    if (userId <= 0) return;
    unsigned long now = millis();

    // Update existing entry
    for (int i = 0; i < MAX_USERS; i++) {
        if (activity[i].used && activity[i].userId == userId) {
            activity[i].lastSeen = now;
            return;
        }
    }
    // Find free slot
    for (int i = 0; i < MAX_USERS; i++) {
        if (!activity[i].used) {
            activity[i].userId   = userId;
            activity[i].lastSeen = now;
            activity[i].used     = true;
            return;
        }
    }
    // No free slot (all used) — recycle the oldest entry
    int oldest = 0;
    for (int i = 1; i < MAX_USERS; i++) {
        if (activity[i].used && activity[i].lastSeen < activity[oldest].lastSeen) oldest = i;
    }
    activity[oldest].userId   = userId;
    activity[oldest].lastSeen = now;
    activity[oldest].used     = true;
}

bool isUserActive(int userId) {
    unsigned long now = millis();
    for (int i = 0; i < MAX_USERS; i++) {
        if (activity[i].used && activity[i].userId == userId) {
            if ((now - activity[i].lastSeen) <= ACTIVITY_TIMEOUT_MS) return true;
            activity[i].used = false;
            return false;
        }
    }
    return false;
}
