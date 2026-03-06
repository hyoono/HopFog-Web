/*
 * web_server.cpp — Serve static files from SD card and set up CORS / 404
 */

#include "web_server.h"
#include "auth.h"
#include "config.h"
#include "sd_storage.h"

#include <SD.h>
#define SD_FS SD

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
    if (!SD_FS.exists(sdPath)) {
        request->send(404, "text/plain", "File not found: " + sdPath);
        return;
    }
    AsyncWebServerResponse *response = request->beginResponse(SD_FS, sdPath, mimeType(sdPath));
    // Cache immutable assets (CSS, JS, fonts, images) for 1 hour.
    // Saves SD card reads and keeps WiFi responsive.
    if (sdPath.endsWith(".css") || sdPath.endsWith(".js") ||
        sdPath.endsWith(".woff2") || sdPath.endsWith(".woff") ||
        sdPath.endsWith(".png") || sdPath.endsWith(".jpg") ||
        sdPath.endsWith(".svg") || sdPath.endsWith(".ico")) {
        response->addHeader("Cache-Control", "public, max-age=3600");
    }
    request->send(response);
}

// ── Auth guard helper ────────────────────────────────────────────────

static bool isAuthenticated(AsyncWebServerRequest *request) {
    if (!request->hasHeader("Cookie")) return false;
    String cookie = request->header("Cookie");
    String token  = extractTokenFromCookie(cookie);
    return validateToken(token) >= 0;
}

static void serveProtectedPage(AsyncWebServerRequest *request, const String &sdPath) {
    if (!isAuthenticated(request)) {
        request->redirect("/login");
        return;
    }
    serveStaticFile(request, sdPath);
}

// ── Setup ───────────────────────────────────────────────────────────

void setupWebServer(AsyncWebServer &server) {

    // Root → login page (public)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveStaticFile(request, "/www/login.html");
    });

    // Public pages (no auth required)
    server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveStaticFile(request, "/www/login.html");
    });
    server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveStaticFile(request, "/www/register.html");
    });

    // Download endpoint (public — accessible from login page)
    server.on("/download/app", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Try APK first, fall back to placeholder text file
        if (SD_FS.exists("/www/HopFog-App.apk")) {
            request->send(SD_FS, "/www/HopFog-App.apk", "application/vnd.android.package-archive");
        } else if (SD_FS.exists("/www/HopFog-App.txt")) {
            request->send(SD_FS, "/www/HopFog-App.txt", "text/plain");
        } else {
            request->send(404, "text/plain", "App file not found on SD card");
        }
    });

    // Protected pages (auth required — redirect to /login if not logged in)
    server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/dashboard.html");
    });
    server.on("/users", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Mobile API: GET /users?user_id=X → JSON user list
        if (request->hasParam("user_id")) {
            int userId = request->getParam("user_id")->value().toInt();
            JsonDocument doc;
            readJsonArray(SD_USERS_FILE, doc);
            JsonDocument resp;
            JsonArray arr = resp.to<JsonArray>();
            for (JsonObject u : doc.as<JsonArray>()) {
                if (!(u["is_active"] | 0)) continue;
                if ((u["id"] | 0) == userId) continue;
                JsonObject o = arr.add<JsonObject>();
                o["id"]       = u["id"];
                o["username"] = u["username"];
            }
            String out;
            serializeJson(resp, out);
            request->send(200, "application/json", out);
            return;
        }
        // Web admin: serve HTML page
        serveProtectedPage(request, "/www/users.html");
    });
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/logs.html");
    });
    server.on("/fog_nodes", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/fog_nodes.html");
    });
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/settings.html");
    });
    server.on("/api-docs", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/api_docs.html");
    });

    // Admin messaging pages — /admin/messaging/*
    // NOTE: Sub-routes MUST be registered BEFORE the parent route because
    // ESPAsyncWebServer's canHandle() treats "/admin/messaging" as matching
    // any URL that starts with "/admin/messaging/".  First match wins.
    server.on("/admin/messaging/broadcasts", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_broadcasts.html");
    });
    server.on("/admin/messaging/broadcast_detail", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_broadcast_detail.html");
    });
    server.on("/admin/messaging/queue", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_queue.html");
    });
    server.on("/admin/messaging/tracking", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_tracking.html");
    });
    server.on("/admin/messaging/sos", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_sos.html");
    });
    server.on("/admin/messaging/testing", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_testing.html");
    });
    server.on("/admin/messaging", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveProtectedPage(request, "/www/admin_messaging.html");
    });

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

    server.on("/static/vendor/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        String uri = request->url();
        String file = uri.substring(String("/static").length());
        serveStaticFile(request, "/www" + file);
    });

    // ── Captive portal detection ────────────────────────────────────
    // Android, iOS, and Windows probe specific URLs to check if the
    // network has internet connectivity.  If these return unexpected
    // responses, the OS may disconnect or deprioritise the WiFi.
    // Returning a proper 204 / redirect keeps the connection stable.

    // Android connectivity checks
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(204);
    });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(204);
    });

    // Apple / iOS captive portal detection
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });

    // Windows NCSI (Network Connectivity Status Indicator)
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });

    // Firefox captive portal detection
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "success\n");
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
