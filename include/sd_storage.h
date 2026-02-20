#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Initialize SD card and create directory structure / seed files
bool initSDCard();

// ── Generic JSON helpers ────────────────────────────────────────────
// Read a JSON array file into a JsonDocument. Returns false on error.
bool readJsonArray(const char *path, JsonDocument &doc);
// Write a JsonDocument (expected to hold an array) back to file.
bool writeJsonArray(const char *path, const JsonDocument &doc);
// Return next auto-increment id for a given collection.
int nextId(const char *path);

// ── User helpers ────────────────────────────────────────────────────
bool     userEmailExists(const char *email);
bool     userNameExists(const char *username);
JsonDocument getUserByEmail(const char *email);
JsonDocument getUserById(int id);
int      createUser(const char *username, const char *email,
                    const char *passwordHash, const char *role, bool active);
bool     updateUserField(int userId, const char *field, const char *value);
bool     toggleUserActive(int userId);

// ── Message helpers ─────────────────────────────────────────────────
int  createMessage(int senderId, const char *subject, const char *body,
                   JsonArray recipientIds);
bool deleteMessage(int messageId);

// ── Fog device helpers ──────────────────────────────────────────────
int  registerFogDevice(const char *name);
bool updateFogDeviceStatus(int deviceId, const char *status,
                           const char *storageUsed, const char *storageTotal,
                           int connectedUsers);
bool disconnectFogDevice(int deviceId);

// ── Broadcast helpers ───────────────────────────────────────────────
int  createBroadcast(int createdBy, const char *msgType, const char *severity,
                     const char *audience, const char *subject, const char *body,
                     const char *status, int priority);

#endif // SD_STORAGE_H
