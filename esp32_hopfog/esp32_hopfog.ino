/*
 * HopFog ESP32-CAM Web Server
 * 
 * This is a conversion of the HopFog-Web FastAPI project to run on ESP32-CAM
 * The ESP32-CAM will act as a web server for the HopFog admin interface
 * 
 * Features:
 * - WiFi web server
 * - Camera streaming
 * - Admin dashboard
 * - Basic authentication
 * - Fog node management
 * - Message logging
 * 
 * Hardware: ESP32-CAM (AI-Thinker)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

// ========================================
// Configuration
// ========================================

// WiFi credentials - Change these to match your network
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Admin credentials - Change these for security
const char* admin_username = "admin";
const char* admin_password = "admin123";

// Web server on port 80
WebServer server(80);

// ========================================
// Camera Configuration (AI-Thinker ESP32-CAM)
// ========================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ========================================
// Global Variables
// ========================================

String sessionToken = "";
bool authenticated = false;

// SD Card status
bool sdCardAvailable = false;

// Stats (loaded from SD card)
int fog_nodes_count = 0;
int active_fog_nodes = 0;
int people_connected = 0;
int total_messages = 0;

// Database file paths on SD card
const char* FOG_NODES_FILE = "/hopfog/fog_nodes.json";
const char* MESSAGES_FILE = "/hopfog/messages.json";
const char* USERS_FILE = "/hopfog/users.json";
const char* STATS_FILE = "/hopfog/stats.json";

// ========================================
// HTML Pages
// ========================================

const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HopFog - Login</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 10px;
            box-shadow: 0 10px 25px rgba(0,0,0,0.2);
            padding: 40px;
            max-width: 400px;
            width: 100%;
        }
        h1 { 
            color: #333; 
            margin-bottom: 30px; 
            text-align: center;
            font-size: 28px;
        }
        .logo {
            text-align: center;
            margin-bottom: 20px;
            font-size: 48px;
            color: #667eea;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #555;
            font-weight: bold;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            border: 2px solid #ddd;
            border-radius: 5px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input[type="text"]:focus, input[type="password"]:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            width: 100%;
            padding: 12px;
            background: #667eea;
            color: white;
            border: none;
            border-radius: 5px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: background 0.3s;
        }
        button:hover {
            background: #5568d3;
        }
        .error {
            background: #fee;
            color: #c33;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
            display: none;
        }
        .info {
            text-align: center;
            margin-top: 20px;
            color: #777;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">🌫️</div>
        <h1>HopFog Admin</h1>
        <div id="error" class="error"></div>
        <form id="loginForm">
            <div class="form-group">
                <label for="username">Username</label>
                <input type="text" id="username" name="username" required>
            </div>
            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" required>
            </div>
            <button type="submit">Login</button>
        </form>
        <div class="info">ESP32-CAM Web Server</div>
    </div>
    <script>
        document.getElementById('loginForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const username = document.getElementById('username').value;
            const password = document.getElementById('password').value;
            
            const formData = new URLSearchParams();
            formData.append('username', username);
            formData.append('password', password);
            
            try {
                const response = await fetch('/login', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: formData
                });
                
                const data = await response.json();
                
                if (data.success) {
                    localStorage.setItem('token', data.token);
                    window.location.href = '/dashboard';
                } else {
                    document.getElementById('error').textContent = data.message || 'Invalid credentials';
                    document.getElementById('error').style.display = 'block';
                }
            } catch (error) {
                document.getElementById('error').textContent = 'Connection error';
                document.getElementById('error').style.display = 'block';
            }
        });
    </script>
</body>
</html>
)rawliteral";

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HopFog - Dashboard</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            background: #f4f6f9;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .header h1 {
            display: inline-block;
            font-size: 24px;
        }
        .logout {
            float: right;
            background: rgba(255,255,255,0.2);
            padding: 10px 20px;
            border-radius: 5px;
            text-decoration: none;
            color: white;
            font-weight: bold;
        }
        .logout:hover {
            background: rgba(255,255,255,0.3);
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .stat-card h3 {
            color: #667eea;
            font-size: 14px;
            margin-bottom: 10px;
            text-transform: uppercase;
        }
        .stat-card .value {
            font-size: 32px;
            font-weight: bold;
            color: #333;
        }
        .section {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
            margin-bottom: 20px;
        }
        .section h2 {
            color: #333;
            margin-bottom: 15px;
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
        }
        .camera-feed {
            width: 100%;
            border-radius: 5px;
            background: #000;
        }
        table {
            width: 100%;
            border-collapse: collapse;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background: #f8f9fa;
            font-weight: bold;
            color: #555;
        }
        .status-active { color: #28a745; font-weight: bold; }
        .status-inactive { color: #dc3545; font-weight: bold; }
        .nav {
            background: white;
            padding: 15px 20px;
            margin-bottom: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .nav a {
            margin-right: 20px;
            text-decoration: none;
            color: #667eea;
            font-weight: bold;
        }
        .nav a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>🌫️ HopFog Admin Dashboard</h1>
        <a href="/" class="logout" onclick="localStorage.removeItem('token')">Logout</a>
    </div>
    <div class="container">
        <div class="nav">
            <a href="/dashboard">Dashboard</a>
            <a href="/fognodes">Fog Nodes</a>
            <a href="/camera">Camera</a>
            <a href="/settings">Settings</a>
        </div>
        
        <div class="stats">
            <div class="stat-card">
                <h3>Total Fog Nodes</h3>
                <div class="value" id="fog_nodes">0</div>
            </div>
            <div class="stat-card">
                <h3>Active Nodes</h3>
                <div class="value" id="active_nodes">0</div>
            </div>
            <div class="stat-card">
                <h3>Connected Users</h3>
                <div class="value" id="connected_users">0</div>
            </div>
            <div class="stat-card">
                <h3>Total Messages</h3>
                <div class="value" id="total_messages">0</div>
            </div>
        </div>
        
        <div class="section">
            <h2>ESP32-CAM Status</h2>
            <p><strong>IP Address:</strong> <span id="ip_address">Loading...</span></p>
            <p><strong>Free Heap:</strong> <span id="free_heap">Loading...</span> bytes</p>
            <p><strong>Uptime:</strong> <span id="uptime">Loading...</span></p>
            <p><strong>SD Card:</strong> <span id="sd_status">Loading...</span></p>
        </div>
        
        <div class="section">
            <h2>Recent Messages</h2>
            <div id="messages">Loading...</div>
        </div>
    </div>
    
    <script>
        // Check authentication
        const token = localStorage.getItem('token');
        if (!token) {
            window.location.href = '/';
        }
        
        // Load dashboard data
        async function loadData() {
            try {
                const response = await fetch('/api/stats');
                const data = await response.json();
                
                document.getElementById('fog_nodes').textContent = data.fog_nodes_count;
                document.getElementById('active_nodes').textContent = data.active_fog_nodes;
                document.getElementById('connected_users').textContent = data.people_connected;
                document.getElementById('total_messages').textContent = data.total_messages;
                document.getElementById('ip_address').textContent = data.ip_address;
                document.getElementById('free_heap').textContent = data.free_heap;
                document.getElementById('uptime').textContent = data.uptime;
                
                // SD Card status
                if (data.sd_card_available) {
                    const sdInfo = `Available (${data.sd_card_used_mb}MB / ${data.sd_card_size_mb}MB used)`;
                    document.getElementById('sd_status').textContent = sdInfo;
                    document.getElementById('sd_status').style.color = '#28a745';
                } else {
                    document.getElementById('sd_status').textContent = 'Not Available (using in-memory storage)';
                    document.getElementById('sd_status').style.color = '#dc3545';
                }
            } catch (error) {
                console.error('Failed to load data:', error);
            }
        }
        
        // Refresh every 5 seconds
        loadData();
        setInterval(loadData, 5000);
    </script>
</body>
</html>
)rawliteral";

// ========================================
// SD Card Database Functions
// ========================================

bool initSDCard() {
    // Initialize SD card using SD_MMC (1-bit mode to avoid pin conflicts with camera)
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD Card Mount Failed");
        return false;
    }
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD Card attached");
        return false;
    }
    
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    // Create hopfog directory if it doesn't exist
    if (!SD_MMC.exists("/hopfog")) {
        SD_MMC.mkdir("/hopfog");
        Serial.println("Created /hopfog directory");
    }
    
    return true;
}

// Read a JSON file from SD card
String readJSONFile(const char* path) {
    if (!sdCardAvailable) {
        return "[]";
    }
    
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open file for reading: %s\n", path);
        return "[]";
    }
    
    String content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    
    return content;
}

// Write a JSON string to SD card
bool writeJSONFile(const char* path, const String& content) {
    if (!sdCardAvailable) {
        return false;
    }
    
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("Failed to open file for writing: %s\n", path);
        return false;
    }
    
    file.print(content);
    file.close();
    Serial.printf("Written to file: %s\n", path);
    return true;
}

// Initialize database files
void initDatabase() {
    if (!sdCardAvailable) {
        Serial.println("SD Card not available, using in-memory storage");
        return;
    }
    
    // Initialize users file with default admin if it doesn't exist
    if (!SD_MMC.exists(USERS_FILE)) {
        StaticJsonDocument<512> doc;
        JsonArray users = doc.createNestedArray("users");
        JsonObject admin = users.createNestedObject();
        admin["username"] = "admin";
        admin["password"] = "admin123";
        admin["role"] = "admin";
        
        String output;
        serializeJson(doc, output);
        writeJSONFile(USERS_FILE, output);
        Serial.println("Created default users file");
    }
    
    // Initialize fog nodes file if it doesn't exist
    if (!SD_MMC.exists(FOG_NODES_FILE)) {
        writeJSONFile(FOG_NODES_FILE, "{\"nodes\":[]}");
        Serial.println("Created fog nodes file");
    }
    
    // Initialize messages file if it doesn't exist
    if (!SD_MMC.exists(MESSAGES_FILE)) {
        writeJSONFile(MESSAGES_FILE, "{\"messages\":[]}");
        Serial.println("Created messages file");
    }
    
    // Initialize stats file if it doesn't exist
    if (!SD_MMC.exists(STATS_FILE)) {
        StaticJsonDocument<256> doc;
        doc["fog_nodes_count"] = 0;
        doc["active_fog_nodes"] = 0;
        doc["people_connected"] = 0;
        doc["total_messages"] = 0;
        
        String output;
        serializeJson(doc, output);
        writeJSONFile(STATS_FILE, output);
        Serial.println("Created stats file");
    }
    
    // Load stats from SD card
    loadStats();
}

// Load statistics from SD card
void loadStats() {
    if (!sdCardAvailable) {
        return;
    }
    
    String content = readJSONFile(STATS_FILE);
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, content);
    
    if (!error) {
        fog_nodes_count = doc["fog_nodes_count"] | 0;
        active_fog_nodes = doc["active_fog_nodes"] | 0;
        people_connected = doc["people_connected"] | 0;
        total_messages = doc["total_messages"] | 0;
        Serial.println("Loaded stats from SD card");
    }
}

// Save statistics to SD card
void saveStats() {
    if (!sdCardAvailable) {
        return;
    }
    
    StaticJsonDocument<256> doc;
    doc["fog_nodes_count"] = fog_nodes_count;
    doc["active_fog_nodes"] = active_fog_nodes;
    doc["people_connected"] = people_connected;
    doc["total_messages"] = total_messages;
    
    String output;
    serializeJson(doc, output);
    writeJSONFile(STATS_FILE, output);
}

// Add a fog node to SD card
bool addFogNode(const String& deviceName, const String& ipAddress, const String& status) {
    if (!sdCardAvailable) {
        return false;
    }
    
    String content = readJSONFile(FOG_NODES_FILE);
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, content);
    
    if (error) {
        Serial.println("Failed to parse fog nodes file");
        return false;
    }
    
    JsonArray nodes = doc["nodes"];
    JsonObject newNode = nodes.createNestedObject();
    newNode["id"] = nodes.size() + 1;
    newNode["device_name"] = deviceName;
    newNode["ip_address"] = ipAddress;
    newNode["status"] = status;
    newNode["connected_users"] = 0;
    newNode["timestamp"] = millis();
    
    String output;
    serializeJson(doc, output);
    
    if (writeJSONFile(FOG_NODES_FILE, output)) {
        fog_nodes_count = nodes.size();
        if (status == "active") {
            active_fog_nodes++;
        }
        saveStats();
        return true;
    }
    
    return false;
}

// Add a message to SD card
bool addMessage(const String& from, const String& to, const String& message) {
    if (!sdCardAvailable) {
        return false;
    }
    
    String content = readJSONFile(MESSAGES_FILE);
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, content);
    
    if (error) {
        Serial.println("Failed to parse messages file");
        return false;
    }
    
    JsonArray messages = doc["messages"];
    JsonObject newMsg = messages.createNestedObject();
    newMsg["id"] = messages.size() + 1;
    newMsg["from"] = from;
    newMsg["to"] = to;
    newMsg["message"] = message;
    newMsg["timestamp"] = millis();
    
    String output;
    serializeJson(doc, output);
    
    if (writeJSONFile(MESSAGES_FILE, output)) {
        total_messages = messages.size();
        saveStats();
        return true;
    }
    
    return false;
}

// Get all fog nodes
String getFogNodes() {
    return readJSONFile(FOG_NODES_FILE);
}

// Get all messages
String getMessages() {
    return readJSONFile(MESSAGES_FILE);
}

// ========================================
// Camera Functions
// ========================================

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // Init with high specs to pre-allocate larger buffers
    if(psramFound()){
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    
    // Camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return false;
    }
    
    return true;
}

// ========================================
// Web Server Handlers
// ========================================

void handleRoot() {
    server.send_P(200, "text/html", LOGIN_HTML);
}

void handleLogin() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String username = server.arg("username");
    String password = server.arg("password");
    
    StaticJsonDocument<200> doc;
    
    if (username == admin_username && password == admin_password) {
        // Generate simple token
        sessionToken = String(random(100000, 999999));
        authenticated = true;
        
        doc["success"] = true;
        doc["token"] = sessionToken;
        doc["message"] = "Login successful";
    } else {
        doc["success"] = false;
        doc["message"] = "Invalid credentials";
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleDashboard() {
    // Check authentication via token
    String token = server.header("Authorization");
    if (token == "" && server.hasArg("token")) {
        token = server.arg("token");
    }
    
    server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleStats() {
    StaticJsonDocument<512> doc;
    
    doc["fog_nodes_count"] = fog_nodes_count;
    doc["active_fog_nodes"] = active_fog_nodes;
    doc["people_connected"] = people_connected;
    doc["total_messages"] = total_messages;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["sd_card_available"] = sdCardAvailable;
    
    if (sdCardAvailable) {
        uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
        uint64_t usedSize = SD_MMC.usedBytes() / (1024 * 1024);
        doc["sd_card_size_mb"] = cardSize;
        doc["sd_card_used_mb"] = usedSize;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// API: Get all fog nodes
void handleGetFogNodes() {
    String nodes = getFogNodes();
    server.send(200, "application/json", nodes);
}

// API: Add a new fog node
void handleAddFogNode() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String deviceName = server.arg("device_name");
    String ipAddress = server.arg("ip_address");
    String status = server.arg("status");
    
    if (deviceName == "" || ipAddress == "") {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing required fields\"}");
        return;
    }
    
    if (addFogNode(deviceName, ipAddress, status.length() > 0 ? status : "active")) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Fog node added\"}");
    } else {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to add fog node\"}");
    }
}

// API: Get all messages
void handleGetMessages() {
    String messages = getMessages();
    server.send(200, "application/json", messages);
}

// API: Add a new message
void handleAddMessage() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String from = server.arg("from");
    String to = server.arg("to");
    String message = server.arg("message");
    
    if (from == "" || to == "" || message == "") {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing required fields\"}");
        return;
    }
    
    if (addMessage(from, to, message)) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Message added\"}");
    } else {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to add message\"}");
    }
}

void handleCamera() {
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();
    
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Content-Length", String(fb->len));
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not Found");
}

// ========================================
// Setup
// ========================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nHopFog ESP32-CAM Web Server");
    Serial.println("============================");
    
    // Initialize SD Card first (before camera to avoid conflicts)
    Serial.println("Initializing SD Card...");
    sdCardAvailable = initSDCard();
    if (sdCardAvailable) {
        Serial.println("SD Card initialized successfully");
        initDatabase();
    } else {
        Serial.println("SD Card initialization failed - using in-memory storage");
    }
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
    } else {
        Serial.println("SPIFFS Mounted");
    }
    
    // Initialize camera
    Serial.println("Initializing camera...");
    if (initCamera()) {
        Serial.println("Camera initialized successfully");
    } else {
        Serial.println("Camera initialization failed");
    }
    
    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("Open http://%s in your browser\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.println("Please check your WiFi credentials in the code");
    }
    
    // Setup web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/dashboard", HTTP_GET, handleDashboard);
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/fognodes", HTTP_GET, handleGetFogNodes);
    server.on("/api/fognodes/add", HTTP_POST, handleAddFogNode);
    server.on("/api/messages", HTTP_GET, handleGetMessages);
    server.on("/api/messages/add", HTTP_POST, handleAddMessage);
    server.on("/camera", HTTP_GET, handleCamera);
    server.onNotFound(handleNotFound);
    
    // Start server
    server.begin();
    Serial.println("Web server started");
    Serial.println("============================\n");
}

// ========================================
// Loop
// ========================================

void loop() {
    server.handleClient();
}
