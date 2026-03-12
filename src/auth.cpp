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

void authInit() {
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        sessions[i].used = false;
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
    // Evict any existing token for this user
    for (int i = 0; i < MAX_ACTIVE_TOKENS; i++) {
        if (sessions[i].used && sessions[i].userId == userId) {
            sessions[i].used = false;
        }
    }

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
