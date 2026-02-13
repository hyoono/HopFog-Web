# HopFog ESP32-CAM Architecture

## System Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HopFog ESP32-CAM System                       │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────┐          WiFi           ┌─────────────────────────┐
│   Web Browser   │ ←──────────────────────→ │    ESP32-CAM Device    │
│  (Dashboard)    │    HTTP Requests         │   (Web Server)          │
└─────────────────┘                          └─────────────────────────┘
                                                        │
                                                        ├──→ Camera Module
                                                        │    (OV2640)
                                                        │
                                                        ├──→ WiFi Module
                                                        │    (2.4GHz)
                                                        │
                                                        └──→ SD Card Slot
                                                             (Database)
```

## Component Breakdown

### 1. ESP32-CAM Hardware Layer
```
┌────────────────────────────────────────────┐
│           ESP32-CAM Module                 │
│  ┌──────────────────────────────────────┐ │
│  │  ESP32 Dual-Core Processor           │ │
│  │  • 240MHz Clock                      │ │
│  │  • 520KB RAM                         │ │
│  │  • 4MB Flash                         │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ┌──────────────┐   ┌──────────────────┐ │
│  │  OV2640      │   │  SD Card Slot    │ │
│  │  Camera      │   │  (FAT32)         │ │
│  │  2MP Sensor  │   │  Up to 32GB      │ │
│  └──────────────┘   └──────────────────┘ │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │  WiFi 802.11 b/g/n (2.4GHz)         │ │
│  └──────────────────────────────────────┘ │
└────────────────────────────────────────────┘
```

### 2. Software Stack
```
┌─────────────────────────────────────────────────────┐
│                   Application Layer                  │
│  ┌────────────┐  ┌─────────────┐  ┌──────────────┐ │
│  │  Web UI    │  │  REST API   │  │  Camera      │ │
│  │  (HTML)    │  │  (JSON)     │  │  Streaming   │ │
│  └────────────┘  └─────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│                Web Server Layer                      │
│  ┌─────────────────────────────────────────────┐   │
│  │  WebServer Library (Arduino)                │   │
│  │  • Route Handling                           │   │
│  │  • HTTP Request/Response                    │   │
│  │  • Session Management                       │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│              Database Access Layer                   │
│  ┌─────────────────────────────────────────────┐   │
│  │  SD Card Manager (SD_MMC)                   │   │
│  │  • File I/O Operations                      │   │
│  │  • JSON Parsing (ArduinoJson)              │   │
│  │  • Data Persistence                         │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────┐
│                 Hardware Layer                       │
│  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │
│  │  WiFi HAL   │  │  Camera HAL │  │  SD HAL    │ │
│  └─────────────┘  └─────────────┘  └────────────┘ │
└─────────────────────────────────────────────────────┘
```

### 3. Database Structure on SD Card
```
/sdcard/
└── hopfog/
    ├── users.json
    │   ├─ User Credentials
    │   ├─ Roles & Permissions
    │   └─ Authentication Data
    │
    ├── fog_nodes.json
    │   ├─ Device Registry
    │   ├─ Status Information
    │   ├─ Connection Details
    │   └─ Metadata
    │
    ├── messages.json
    │   ├─ Message Log
    │   ├─ Sender/Receiver Info
    │   ├─ Timestamps
    │   └─ Content
    │
    └── stats.json
        ├─ System Statistics
        ├─ Performance Metrics
        └─ Aggregate Data
```

### 4. Request Flow Diagram
```
┌─────────────┐
│   Browser   │
└──────┬──────┘
       │ 1. HTTP Request
       ↓
┌─────────────────┐
│  WiFi Interface │
└────────┬────────┘
         │ 2. Route to Handler
         ↓
┌─────────────────┐
│  WebServer      │
│  Route Handler  │
└────────┬────────┘
         │ 3. Process Request
         ↓
┌─────────────────┐    4. Read/Write    ┌──────────────┐
│  Application    │ ←─────────────────→ │  SD Card DB  │
│  Logic          │                      │  (JSON)      │
└────────┬────────┘                      └──────────────┘
         │ 5. Generate Response
         ↓
┌─────────────────┐
│  JSON/HTML      │
│  Response       │
└────────┬────────┘
         │ 6. Send Response
         ↓
┌─────────────────┐
│   Browser       │
└─────────────────┘
```

### 5. API Endpoints Map
```
/
├── GET  /                     → Login Page
├── POST /login                → Authentication
├── GET  /dashboard            → Main Dashboard
│
├── /api/
│   ├── GET  /api/stats        → System Statistics
│   ├── GET  /api/fognodes     → List Fog Nodes
│   ├── POST /api/fognodes/add → Add Fog Node
│   ├── GET  /api/messages     → List Messages
│   └── POST /api/messages/add → Add Message
│
└── /camera                    → Camera Image (JPEG)
```

### 6. Memory Layout
```
┌────────────────────────────────────┐
│     ESP32 Memory (520KB RAM)       │
├────────────────────────────────────┤
│  Program Code (Flash)   │ ~200KB  │  ← Loaded from Flash
├─────────────────────────┼─────────┤
│  HTML Pages (PROGMEM)   │ ~50KB   │  ← Stored in Flash
├─────────────────────────┼─────────┤
│  WebServer Buffers      │ ~100KB  │  ← Dynamic allocation
├─────────────────────────┼─────────┤
│  Camera Frame Buffer    │ ~100KB  │  ← PSRAM if available
├─────────────────────────┼─────────┤
│  JSON Processing        │ ~20KB   │  ← Dynamic allocation
├─────────────────────────┼─────────┤
│  Stack & Variables      │ ~30KB   │  ← Runtime usage
├─────────────────────────┼─────────┤
│  Free Heap              │ ~20KB   │  ← Available memory
└────────────────────────────────────┘

SD Card Storage (GB scale)
└── Persistent database files
```

### 7. Power Consumption
```
┌────────────────────────────────────┐
│         Power Profile              │
├────────────────────────────────────┤
│  Idle State          │  ~100 mA    │
│  WiFi Connected      │  ~150 mA    │
│  Serving Web Page    │  ~250 mA    │
│  Camera Capture      │  ~350 mA    │
│  Flash LED Active    │  ~500 mA    │
└────────────────────────────────────┘

Voltage: 5V DC
Recommended Supply: 500-1000mA
```

### 8. Security Model
```
┌─────────────────────────────────────┐
│         Security Layers             │
├─────────────────────────────────────┤
│  Session Token                      │
│  └─ Simple random token auth        │
│                                     │
│  Credential Storage                 │
│  └─ Plaintext on SD card           │
│     (suitable for trusted network)  │
│                                     │
│  Network Security                   │
│  └─ HTTP (no HTTPS)                │
│     (local network only)            │
└─────────────────────────────────────┘

⚠️  Suitable for trusted networks only
⚠️  Not exposed to internet directly
```

### 9. Deployment Scenarios

#### Scenario A: Standalone Operation
```
[ESP32-CAM] ──→ [Battery Pack]
     │
     └──→ WiFi Network (Local)
              │
              └──→ [Admin's Laptop]
```

#### Scenario B: Fog Node Integration
```
[ESP32-CAM Hub]
     ├──→ [Fog Node 1]
     ├──→ [Fog Node 2]
     ├──→ [Fog Node 3]
     └──→ [Admin Dashboard]
```

#### Scenario C: Multi-Site Deployment
```
[Site A] ESP32-CAM ──┐
[Site B] ESP32-CAM ──┼──→ [Central Monitoring]
[Site C] ESP32-CAM ──┘
```

## Performance Characteristics

```
┌─────────────────────────────────────┐
│     Performance Metrics             │
├─────────────────────────────────────┤
│  Page Load Time      │  100-300ms   │
│  API Response Time   │  50-150ms    │
│  Camera Capture      │  50-100ms    │
│  SD Card Write       │  10-50ms     │
│  Concurrent Users    │  4-5 max     │
│  Uptime             │  Days-Weeks   │
└─────────────────────────────────────┘
```

## Development vs Production

### Development Setup
```
[Computer] ──USB──→ [FTDI] ──→ [ESP32-CAM]
                                    │
                                    └──→ Serial Monitor
```

### Production Setup
```
[Power Supply 5V] ──→ [ESP32-CAM] ←──WiFi──→ [Network]
                           │
                           └──→ [Fog Network]
```

---

## Quick Reference

### Pin Usage
- **Camera Pins**: GPIO 0, 5, 18, 19, 21, 23, 25, 26, 27, 32, 34, 35, 36, 39
- **SD Card**: GPIO 2, 4, 12, 13, 14, 15 (1-bit mode)
- **Status LED**: GPIO 33
- **Flash LED**: GPIO 4
- **Serial**: GPIO 1 (TX), GPIO 3 (RX)

### Memory Budget
- Flash: 4MB (program + SPIFFS)
- RAM: 520KB (runtime)
- PSRAM: 4MB (if available, for camera)
- SD Card: Up to 32GB (database)

### Network
- Protocol: HTTP/1.1
- Port: 80 (default)
- WiFi: 2.4GHz 802.11 b/g/n
- Max Clients: 4-5 simultaneous

---

This architecture provides a balance between functionality, simplicity, and resource constraints of the ESP32-CAM platform.
