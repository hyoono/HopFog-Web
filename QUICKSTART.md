# HopFog ESP32-CAM Quick Start Guide

Get your HopFog ESP32-CAM web server running in 15 minutes!

## 📋 What You Need

- [ ] ESP32-CAM module (AI-Thinker)
- [ ] FTDI programmer or USB-to-TTL adapter
- [ ] MicroSD card (4-32GB, Class 10)
- [ ] 6 jumper wires
- [ ] Computer with Arduino IDE
- [ ] WiFi network

## 🚀 Quick Setup (5 Steps)

### Step 1: Install Software (5 minutes)

1. Download [Arduino IDE](https://www.arduino.cc/en/software)
2. Open Arduino IDE → **File → Preferences**
3. Add to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools → Board → Boards Manager**
5. Search "ESP32" → Install "esp32 by Espressif Systems"
6. Go to **Tools → Manage Libraries**
7. Search "ArduinoJson" → Install "ArduinoJson by Benoit Blanchon"

### Step 2: Prepare SD Card (2 minutes)

1. Format your microSD card as **FAT32**
2. Insert into ESP32-CAM SD card slot (contacts facing board)

### Step 3: Connect Hardware (3 minutes)

Connect ESP32-CAM to FTDI programmer:

| ESP32-CAM | FTDI |
|-----------|------|
| GND | GND |
| 5V | VCC (5V) |
| U0R | TX |
| U0T | RX |
| **IO0** | **GND** ← Important! |

> 💡 **Remember**: IO0 to GND is only for uploading!

### Step 4: Configure and Upload (3 minutes)

1. Open `esp32_hopfog/esp32_hopfog.ino` in Arduino IDE
2. Edit lines 29-30 with your WiFi:
   ```cpp
   const char* ssid = "YOUR_WIFI_NAME";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
3. Select **Tools → Board → AI Thinker ESP32-CAM**
4. Select **Tools → Port → (your FTDI port)**
5. Click **Upload** button (→)
6. Wait for "Done uploading"

### Step 5: Run! (2 minutes)

1. **Disconnect IO0 from GND** (very important!)
2. Press **RESET** button on ESP32-CAM
3. Open **Tools → Serial Monitor** (set to 115200 baud)
4. Look for the IP address:
   ```
   WiFi connected!
   IP address: 192.168.1.100
   Open http://192.168.1.100 in your browser
   ```
5. Open that URL in your browser
6. Login with:
   - Username: `admin`
   - Password: `admin123`

## 🎉 Success!

You should now see the HopFog dashboard!

---

## 🧪 Test Your Setup

Run the API test script:

```bash
cd esp32_hopfog
python test_api.py 192.168.1.100  # Use your ESP32's IP
```

## 📊 What You Can Do

### Via Web Interface:
- View dashboard statistics
- Monitor ESP32-CAM status
- Check SD card storage
- View fog nodes
- See messages

### Via API:
```bash
# Add a fog node
curl -X POST http://192.168.1.100/api/fognodes/add \
  -d "device_name=MyNode&ip_address=192.168.1.50&status=active"

# Add a message
curl -X POST http://192.168.1.100/api/messages/add \
  -d "from=user1&to=user2&message=Hello"

# Get statistics
curl http://192.168.1.100/api/stats

# Get camera image
curl http://192.168.1.100/camera -o photo.jpg
```

## ❓ Troubleshooting

### Can't Upload Code
- ✅ Check IO0 is connected to GND
- ✅ Try pressing RESET when you see "Connecting..."
- ✅ Verify RX/TX are crossed correctly

### Can't Connect to WiFi
- ✅ Check WiFi name and password
- ✅ Use 2.4GHz network (ESP32 doesn't support 5GHz)
- ✅ Check Serial Monitor for error messages

### SD Card Not Detected
- ✅ Ensure formatted as FAT32
- ✅ Try a different SD card
- ✅ Check it's inserted correctly (contacts facing board)
- ⚠️ System works without SD card, but data won't persist

### Can't Access Web Page
- ✅ Check you're on same WiFi network
- ✅ Verify IP address from Serial Monitor
- ✅ Try pinging the IP: `ping 192.168.1.100`

## 🔒 Security

**Important**: Change default password for production use!

Edit lines 34-35 in the code:
```cpp
const char* admin_username = "your_username";
const char* admin_password = "your_secure_password";
```

Then re-upload the code.

## 📚 Learn More

- **Full Documentation**: [ESP32_README.md](ESP32_README.md)
- **Wiring Details**: [WIRING_GUIDE.md](WIRING_GUIDE.md)
- **Main Project**: [README.md](README.md)

## 💡 Tips

1. **Power**: Use a good 5V supply (500mA minimum)
2. **Range**: Keep ESP32-CAM close to WiFi router initially
3. **SD Card**: Use quality cards (SanDisk, Samsung, Kingston)
4. **Debugging**: Serial Monitor is your friend (115200 baud)
5. **Updates**: Disconnect IO0-GND after upload, before pressing RESET

## 🛠️ Next Steps

Once running:
1. Change default credentials
2. Add your fog nodes via API
3. Test camera functionality
4. Set up your HopFog network
5. Monitor from the dashboard

## 📞 Need Help?

Check the detailed guides:
- Hardware issues → [WIRING_GUIDE.md](WIRING_GUIDE.md)
- Software issues → [ESP32_README.md](ESP32_README.md)
- API usage → Run `python test_api.py --help`

---

**Estimated Total Time**: 15 minutes from start to running web server!

Happy HopFog-ing! 🌫️
