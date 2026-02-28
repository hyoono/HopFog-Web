# HopFog ESP32-CAM Web Server (No Camera)

This is a conversion of the HopFog-Web FastAPI project to run on an ESP32-CAM microcontroller. The ESP32-CAM acts as a standalone web server for the HopFog admin interface without using the camera functionality.

## Hardware Requirements

- **ESP32-CAM** (AI-Thinker model - camera not used)
- **MicroSD Card** (Class 10, 4GB-32GB recommended for database storage)
- **FTDI Programmer** or USB-to-TTL adapter for uploading code
- **5V Power Supply** (USB or external)
- **WiFi Network** for connectivity

## Features

- ✅ WiFi web server running on ESP32-CAM
- ✅ Web-based admin dashboard
- ✅ Basic authentication system
- ✅ Real-time statistics display
- ✅ Fog node management interface
- ✅ **SD Card persistent storage for database**
- ✅ Message logging with persistence
- ✅ User management on SD card
- ✅ Lightweight and embedded-friendly

**Note:** This version does not use the camera hardware on the ESP32-CAM module.

## Installation

### 1. Install Arduino IDE

Download and install the [Arduino IDE](https://www.arduino.cc/en/software) (version 1.8.x or 2.x).

### 2. Install ESP32 Board Support

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add this URL to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools → Board → Boards Manager**
5. Search for "ESP32" and install **"esp32 by Espressif Systems"**

### 3. Install Required Libraries

Go to **Tools → Manage Libraries** and install:

- **ArduinoJson** by Benoit Blanchon (version 6.x)

Other libraries (WiFi, WebServer, SPIFFS, SD_MMC) are included with the ESP32 board package.

### 4. Configure WiFi Credentials

Open the `esp32_hopfog.ino` file and update these lines with your WiFi credentials:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 5. Configure Admin Credentials (Optional)

By default, the admin credentials are:
- Username: `admin`
- Password: `admin123`

To change them, update these lines in the code:

```cpp
const char* admin_username = "admin";
const char* admin_password = "admin123";
```

### 6. Select Board and Port

1. Go to **Tools → Board → ESP32 Arduino → AI Thinker ESP32-CAM**
2. Select the correct **Port** (the COM port of your FTDI programmer)

### 7. Upload the Code

1. Connect your ESP32-CAM to the FTDI programmer:
   - **ESP32-CAM GND** → **FTDI GND**
   - **ESP32-CAM 5V** → **FTDI VCC (5V)**
   - **ESP32-CAM U0R** → **FTDI TX**
   - **ESP32-CAM U0T** → **FTDI RX**
   - **ESP32-CAM IO0** → **FTDI GND** (for programming mode)

2. Click **Upload** button in Arduino IDE
3. Wait for upload to complete
4. Disconnect **IO0 from GND** and press the **RESET** button on ESP32-CAM

## Usage

### 1. Power On

After uploading, power on the ESP32-CAM (disconnect IO0 from GND first, then press RESET).

### 2. Check Serial Monitor

1. Open **Tools → Serial Monitor** (115200 baud rate)
2. You should see output like:
   ```
   HopFog ESP32-CAM Web Server
   ============================
   Connecting to WiFi: YourWiFiName
   ..........
   WiFi connected!
   IP address: 192.168.1.100
   Open http://192.168.1.100 in your browser
   ```

### 3. Access Web Interface

1. Open a web browser on a device connected to the same WiFi network
2. Navigate to the IP address shown in the Serial Monitor
3. Log in with the admin credentials you configured

### 4. Dashboard Features

The web dashboard provides:
- **Statistics**: View fog node count, active nodes, connected users, and messages
- **ESP32-CAM Status**: IP address, free memory, uptime, **SD card status**
- **Real-time Updates**: Dashboard refreshes every 5 seconds
- **Persistent Storage**: All data is saved to the SD card and survives reboots

## SD Card Database

### Overview

The ESP32-CAM uses a microSD card for persistent storage of all database information. This allows data to survive power cycles and reboots.

### SD Card Setup

1. **Format the SD Card**: Use FAT32 format (recommended for cards up to 32GB)
2. **Insert into ESP32-CAM**: Insert the microSD card into the SD card slot on the ESP32-CAM module
3. **Automatic Initialization**: The system will automatically create the necessary directory structure on first boot

### Database Structure

The database is stored as JSON files on the SD card in the `/hopfog/` directory:

```
/sdcard/
└── hopfog/
    ├── users.json        # User credentials and roles
    ├── fog_nodes.json    # Fog device information
    ├── messages.json     # Message logs
    └── stats.json        # System statistics
```

### Database Files

#### users.json
Stores user credentials (default admin user is created automatically):
```json
{
  "users": [
    {
      "username": "admin",
      "password": "admin123",
      "role": "admin"
    }
  ]
}
```

#### fog_nodes.json
Stores fog node information:
```json
{
  "nodes": [
    {
      "id": 1,
      "device_name": "FogNode-01",
      "ip_address": "192.168.1.50",
      "status": "active",
      "connected_users": 5,
      "timestamp": 123456
    }
  ]
}
```

#### messages.json
Stores message logs:
```json
{
  "messages": [
    {
      "id": 1,
      "from": "user1",
      "to": "user2",
      "message": "Hello",
      "timestamp": 123456
    }
  ]
}
```

#### stats.json
Stores system statistics:
```json
{
  "fog_nodes_count": 1,
  "active_fog_nodes": 1,
  "people_connected": 5,
  "total_messages": 10
}
```

### Fallback Mode

If no SD card is detected or initialization fails, the system automatically falls back to **in-memory storage**. In this mode:
- All data is stored in RAM
- Data is lost on reboot
- The dashboard will show "SD Card: Not Available"
- API endpoints still work, but data is not persistent

### SD Card Best Practices

1. **Use Quality Cards**: Use Class 10 or higher microSD cards from reputable brands
2. **Regular Backups**: Periodically back up the `/hopfog/` directory
3. **Avoid Removal**: Don't remove the SD card while the ESP32-CAM is powered on
4. **Monitor Usage**: Check SD card usage in the dashboard to avoid running out of space
5. **Capacity**: 4GB-32GB cards are ideal (FAT32 limitation)

## API Endpoints

The ESP32-CAM web server provides the following endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Login page |
| `/login` | POST | Authenticate user |
| `/dashboard` | GET | Main dashboard page |
| `/api/stats` | GET | Get system statistics (JSON) |
| `/api/fognodes` | GET | Get all fog nodes (JSON) |
| `/api/fognodes/add` | POST | Add a new fog node |
| `/api/messages` | GET | Get all messages (JSON) |
| `/api/messages/add` | POST | Add a new message |

### Example API Usage

**Add a Fog Node:**
```bash
curl -X POST http://192.168.1.100/api/fognodes/add \
  -d "device_name=FogNode-01&ip_address=192.168.1.50&status=active"
```

**Add a Message:**
```bash
curl -X POST http://192.168.1.100/api/messages/add \
  -d "from=user1&to=user2&message=Hello"
```

**Get Statistics:**
```bash
curl http://192.168.1.100/api/stats
```

## Configuration

### WiFi Settings

Configure your WiFi credentials in the code:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### Admin Credentials

Update the default admin credentials before deployment:

```cpp
const char* admin_username = "admin";
const char* admin_password = "admin123";
```

## Troubleshooting

### WiFi Connection Failed

- Verify your WiFi SSID and password are correct
- Ensure the ESP32-CAM is within WiFi range
- Try using a 2.4GHz network (ESP32 doesn't support 5GHz)

### SD Card Not Detected

- Ensure the SD card is properly inserted
- Verify the SD card is formatted as FAT32
- Try a different SD card (Class 10 recommended)
- Check Serial Monitor for specific error messages
- Note: The system will work without SD card but data won't persist

### Upload Failed

- Ensure IO0 is connected to GND during upload
- Check FTDI connections (especially RX/TX - they should be crossed)
- Try lowering upload speed: **Tools → Upload Speed → 115200**

### Can't Access Web Interface

- Verify you're on the same WiFi network as the ESP32-CAM
- Check the Serial Monitor for the correct IP address
- Try disabling your device's firewall temporarily

### Data Not Persisting

- Check if SD card is properly detected (view dashboard)
- Verify SD card has free space
- Check Serial Monitor for write errors
- Try reformatting the SD card as FAT32

## Memory Considerations

The ESP32-CAM has limited memory compared to a full computer. The web pages are stored in program memory (PROGMEM) to save RAM. The SD card provides additional storage for data persistence.

**Storage Overview:**
- **Program Memory**: HTML pages, code (~1MB)
- **RAM**: Runtime variables, buffers (~520KB total, ~100KB free)
- **SPIFFS**: Optional file storage (~4MB)
- **SD Card**: Database files (up to card capacity, typically 4-32GB)

## Differences from Original FastAPI Version

The ESP32-CAM version is adapted for embedded systems compared to the original FastAPI version:

| Feature | FastAPI Version | ESP32-CAM Version |
|---------|----------------|-------------------|
| Database | PostgreSQL/SQLite | **JSON files on SD Card** |
| Authentication | JWT tokens + bcrypt | Simple token + hardcoded credentials |
| Templates | Jinja2 | HTML in PROGMEM |
| Static Files | Separate CSS/JS files | Embedded in HTML |
| Storage | Disk-based | **SD Card (FAT32)** |
| Fog Node Management | Full CRUD operations | Add/View operations via API |
| Message Logging | Database persistence | **SD Card persistence** |
| Persistence | Always | **Depends on SD card availability** |

## Extending Functionality

To add more features to the ESP32-CAM version:

1. **Enhanced Database Operations**: Add update/delete operations for fog nodes and messages
2. **API Integration**: Connect to external services via HTTP requests
3. **MQTT Support**: Integrate MQTT for real-time messaging with fog nodes
4. **Bluetooth**: Add BLE support for local connectivity
5. **Sensors**: Connect additional sensors to I2C/SPI pins
6. **CSV Export**: Export SD card data to CSV format via API
7. **Web File Browser**: Add endpoint to browse SD card files

## Hardware Pinout (AI-Thinker ESP32-CAM)

```
                  ┌─────────────┐
         GND   1  │             │  36  GPIO 32 (LED)
         3.3V  2  │             │  35  GND
         GPIO 4 3 │             │  34  5V
         U0R   4  │  ESP32-CAM  │  33  GPIO 33
         U0T   5  │             │  32  GPIO 1 (TX)
         GND   6  │  AI-Thinker │  31  GPIO 3 (RX)
         5V    7  │             │  30  GND
                  └─────────────┘
```

**Important Pins:**
- **GPIO 0 (IO0)**: Must be pulled LOW for programming mode
- **GPIO 33**: Built-in LED (can be used for status indication)
- **GPIO 4**: Flash LED (high-power, use with caution)

## Power Requirements

- **Voltage**: 5V (via USB or external supply)
- **Current**: 500mA minimum recommended for stable operation
- **Note**: Some USB ports may not provide enough current; use a powered USB hub or external 5V supply if experiencing brownouts

## Security Considerations

⚠️ **Important Security Notes:**

1. **Change Default Credentials**: The default username/password should be changed in production
2. **HTTPS**: The ESP32-CAM uses HTTP (not HTTPS). Don't expose it directly to the internet
3. **Authentication**: The authentication is basic. For production, implement proper token validation
4. **Network Security**: Use on a secure, trusted WiFi network only
5. **Firewall**: Consider putting the ESP32-CAM behind a firewall or VPN

## Performance

- **Response Time**: ~100-300ms for page loads
- **API Response**: ~50-150ms
- **Concurrent Connections**: 4-5 maximum (ESP32 limitation)
- **Uptime**: Can run continuously for weeks (recommend weekly reboots)

## License

Same as the original HopFog-Web project.

## Support

For issues specific to the ESP32-CAM version:
1. Check the Serial Monitor output for error messages
2. Verify all hardware connections
3. Ensure you have the latest ESP32 board package installed
4. Review the troubleshooting section above

For issues with the original HopFog system, refer to the main project documentation.

## Credits

- Original HopFog-Web project: FastAPI-based admin interface
- ESP32-CAM conversion: Adapted for embedded systems
- ESP32 Arduino Core: Espressif Systems
- ArduinoJson: Benoit Blanchon
