# HopFog-Web

This repository contains **two versions** of the HopFog web admin interface:

1. **FastAPI Version** (Original) - Full-featured Python web application
2. **ESP32-CAM Version** (New) - Embedded web server for ESP32-CAM microcontroller

## 🌫️ What is HopFog?

HopFog is a fog computing system that manages communication between edge devices. This web admin interface allows administrators to monitor and manage the HopFog network.

---

## 📦 Version 1: FastAPI Web Application (Original)

### Overview
The original FastAPI-based web admin dashboard for HopFog. This runs on a server or computer and provides a full-featured administration interface.

### Features
- Full Python FastAPI web application
- PostgreSQL/SQLite database support
- Advanced authentication with JWT tokens and bcrypt
- Jinja2 templating
- Mobile app API support
- Full CRUD operations for all resources
- User management with roles
- Message system
- Fog node monitoring
- Real-time statistics

### Setup

**Requirements:**
- Python 3.7+
- Virtual environment
- FastAPI and dependencies

**Installation:**
```bash
# Create virtual environment
python -m venv env

# Activate virtual environment
source env/bin/activate  # On Windows: env\Scripts\activate

# Install FastAPI
pip install fastapi uvicorn sqlalchemy python-dotenv bcrypt

# Run the application
uvicorn app.main:app --reload
```

### Directory Structure
```
HopFog-Web/
├── app/
│   └── main.py           # Main FastAPI application
├── database/
│   ├── connection.py     # Database connection
│   ├── models.py         # Database models
│   └── deps.py          # Dependencies
├── routes/               # API routes
├── services/            # Business logic
├── static/              # CSS, JS, images
└── templates/           # HTML templates
```

---

## 🎯 Version 2: ESP32-CAM Web Server (No Camera)

### Overview
A completely redesigned embedded version that runs directly on an ESP32-CAM microcontroller without using the camera functionality. The ESP32-CAM acts as a standalone web server.

### Features
- Runs on ESP32-CAM hardware (AI-Thinker) - camera not used
- WiFi web server
- **SD Card persistent database**
- Simplified web interface
- RESTful API
- Real-time statistics
- No external server required
- Low power consumption
- Portable and embedded

### Why ESP32-CAM (without camera)?
- **Cost-effective**: ~$10 device vs dedicated server
- **Portable**: Battery-powered option
- **WiFi Built-in**: Direct network connectivity
- **SD Card Slot**: Built-in storage for database
- **Low Power**: Can run 24/7 on minimal power
- **Standalone**: No external dependencies

### Quick Start

See **[ESP32_README.md](ESP32_README.md)** for detailed instructions.

**Quick Setup:**
1. Install Arduino IDE
2. Add ESP32 board support
3. Install ArduinoJson library
4. Open `esp32_hopfog/esp32_hopfog.ino`
5. Configure WiFi credentials
6. Insert microSD card (for persistent storage)
7. Upload to ESP32-CAM
8. Access web interface at ESP32-CAM's IP address

### Hardware Requirements
- ESP32-CAM module (AI-Thinker) - camera functionality not used
- **MicroSD Card (4-32GB, Class 10)**
- FTDI programmer for uploading
- 5V power supply

### API Testing
Test the ESP32-CAM API with provided scripts:

```bash
# Using Python (recommended)
cd esp32_hopfog
python test_api.py 192.168.1.100

# Using Bash
./test_api.sh
```

---

## 🔄 Comparison

| Feature | FastAPI Version | ESP32-CAM Version |
|---------|----------------|-------------------|
| **Platform** | Server/Computer | ESP32-CAM Microcontroller |
| **Language** | Python | C++ (Arduino) |
| **Database** | PostgreSQL/SQLite | **JSON files on SD Card** |
| **Persistence** | Always | **SD Card required** |
| **Authentication** | JWT + bcrypt | Simple token |
| **Power Usage** | High (server) | Low (5V, <500mA) |
| **Cost** | High (server) | Low (~$10 hardware) |
| **Camera** | Not included | Hardware present, not used |
| **Portability** | Requires server | Fully portable |
| **Setup** | Complex | Simple |
| **API** | Full REST API | Simplified REST API |
| **Concurrent Users** | High (100+) | Limited (4-5) |
| **Best For** | Production systems | Edge deployment, demos, IoT |

---

## 🚀 Which Version Should I Use?

### Use FastAPI Version if you need:
- Full-featured production system
- Many concurrent users
- Complex database queries
- Advanced authentication
- Existing server infrastructure
- Integration with other systems

### Use ESP32-CAM Version if you need:
- Low-cost deployment
- Edge computing
- Portable/mobile deployment
- Battery-powered option
- Built-in SD card slot
- Standalone operation
- IoT integration
- Quick prototyping

---

## 📁 Repository Structure

```
HopFog-Web/
├── app/                    # FastAPI application
├── database/               # Database models and connection
├── routes/                 # API routes (FastAPI)
├── services/              # Business logic
├── static/                # CSS, JS, images (FastAPI)
├── templates/             # HTML templates (FastAPI)
├── esp32_hopfog/          # ESP32-CAM version (no camera)
│   ├── esp32_hopfog.ino   # Main Arduino sketch
│   ├── test_api.py        # Python API test script
│   └── test_api.sh        # Bash API test script
├── README.md              # This file
└── ESP32_README.md        # Detailed ESP32-CAM documentation
```

---

## 🛠️ Development

### FastAPI Development
```bash
# Create virtual environment
python -m venv env
source env/bin/activate

# Install dependencies
pip install fastapi uvicorn sqlalchemy python-dotenv bcrypt

# Run development server
uvicorn app.main:app --reload
```

### ESP32-CAM Development
1. Open `esp32_hopfog/esp32_hopfog.ino` in Arduino IDE
2. Make changes
3. Upload to ESP32-CAM
4. Monitor Serial output (115200 baud)
5. Test API with provided scripts

---

## 📚 Documentation

- **FastAPI Version**: See code comments in `app/main.py`
- **ESP32-CAM Version**: See [ESP32_README.md](ESP32_README.md)

---

## 🤝 Contributing

Contributions are welcome for both versions! Please follow the existing code style and test your changes.

### For FastAPI Version:
- Follow PEP 8 style guide
- Add tests for new features
- Update documentation

### For ESP32-CAM Version:
- Follow Arduino coding style
- Test on actual hardware when possible
- Keep memory usage minimal
- Update ESP32_README.md
- Note: Camera functionality is not used

---

## 📄 License

This project is for the HopFog system administration.

---

## 🔗 Quick Links

- [ESP32-CAM Setup Guide](ESP32_README.md)
- [Arduino IDE](https://www.arduino.cc/en/software)
- [FastAPI Documentation](https://fastapi.tiangolo.com/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) 
