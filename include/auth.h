#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

// Initialize auth subsystem
void authInit();

// Hash a plaintext password (simple SHA-256 based hash for ESP32)
String hashPassword(const String &password);

// Verify a plaintext password against a stored hash
bool verifyPassword(const String &password, const String &storedHash);

// Create a session token for a user id, returns the token string
String createSessionToken(int userId);

// Validate a token. Returns the user id, or -1 if invalid.
int validateToken(const String &token);

// Remove a session token (logout)
void removeToken(const String &token);

// Extract token from cookie header value ("access_token=Bearer <token>")
String extractTokenFromCookie(const String &cookieHeader);

#endif // AUTH_H
