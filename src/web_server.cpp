/*
 * web_server.cpp — Serve static files from SD card and set up CORS / 404
 */

#include "web_server.h"
#include "config.h"

#include <SD.h>

// ── MIME type helper ────────────────────────────────────────────────

static const char *mimeType(const String &path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".woff")) return "font/woff";
    if (path.endsWith(".woff2")) return "font/woff2";
    return "application/octet-stream";
}

// ── Static file handler ─────────────────────────────────────────────

static void serveStaticFile(AsyncWebServerRequest *request, const String &sdPath) {
    if (!SD.exists(sdPath)) {
        request->send(404, "text/plain", "File not found: " + sdPath);
        return;
    }
    request->send(SD, sdPath, mimeType(sdPath));
}

// ── Setup ───────────────────────────────────────────────────────────

void setupWebServer(AsyncWebServer &server) {

    // Root → login page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveStaticFile(request, "/www/login.html");
    });

    // Named page routes (browser navigation)
    const char *pages[] = {
        "login", "register", "dashboard", "users",
        "logs", "fog_nodes", "settings", NULL
    };
    for (int i = 0; pages[i] != NULL; i++) {
        String pageName = String(pages[i]);
        server.on(("/" + pageName).c_str(), HTTP_GET,
                  [pageName](AsyncWebServerRequest *request) {
            serveStaticFile(request, "/www/" + pageName + ".html");
        });
    }

    // Admin messaging sub-pages
    const char *adminPages[] = {
        "messaging", "broadcasts", "broadcast_detail",
        "sos", "queue", "tracking", "testing", NULL
    };
    for (int i = 0; adminPages[i] != NULL; i++) {
        String pageName = String(adminPages[i]);
        server.on(("/admin/" + pageName).c_str(), HTTP_GET,
                  [pageName](AsyncWebServerRequest *request) {
            serveStaticFile(request, "/www/admin_" + pageName + ".html");
        });
    }

    // Static assets: /static/css/*, /static/js/*, /static/images/*
    server.on("/static/css/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        String uri = request->url();
        String file = uri.substring(String("/static").length()); // e.g. /css/styles.css
        serveStaticFile(request, "/www" + file);
    });

    server.on("/static/js/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        String uri = request->url();
        String file = uri.substring(String("/static").length());
        serveStaticFile(request, "/www" + file);
    });

    server.on("/static/images/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        String uri = request->url();
        String file = uri.substring(String("/static").length());
        serveStaticFile(request, "/www" + file);
    });

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "404 — Not Found");
    });

    // Default headers (CORS for local development)
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    Serial.println("[HTTP] Static file routes registered");
}
