#!/bin/bash

# HopFog ESP32-CAM API Test Script
# This script helps test the API endpoints of the ESP32-CAM web server

# Configuration - Update this with your ESP32-CAM's IP address
ESP32_IP="192.168.1.100"
BASE_URL="http://${ESP32_IP}"

echo "================================"
echo "HopFog ESP32-CAM API Test Script"
echo "================================"
echo "Testing ESP32-CAM at: $BASE_URL"
echo ""

# Test 1: Get Statistics
echo "1. Testing GET /api/stats"
echo "   Request: curl ${BASE_URL}/api/stats"
curl -s "${BASE_URL}/api/stats" | python3 -m json.tool
echo ""
echo ""

# Test 2: Get Fog Nodes
echo "2. Testing GET /api/fognodes"
echo "   Request: curl ${BASE_URL}/api/fognodes"
curl -s "${BASE_URL}/api/fognodes" | python3 -m json.tool
echo ""
echo ""

# Test 3: Add a Fog Node
echo "3. Testing POST /api/fognodes/add"
echo "   Request: curl -X POST ${BASE_URL}/api/fognodes/add -d 'device_name=TestNode&ip_address=192.168.1.50&status=active'"
curl -s -X POST "${BASE_URL}/api/fognodes/add" \
  -d "device_name=TestNode&ip_address=192.168.1.50&status=active"
echo ""
echo ""

# Test 4: Get Fog Nodes Again (to verify addition)
echo "4. Verifying fog node addition"
echo "   Request: curl ${BASE_URL}/api/fognodes"
curl -s "${BASE_URL}/api/fognodes" | python3 -m json.tool
echo ""
echo ""

# Test 5: Get Messages
echo "5. Testing GET /api/messages"
echo "   Request: curl ${BASE_URL}/api/messages"
curl -s "${BASE_URL}/api/messages" | python3 -m json.tool
echo ""
echo ""

# Test 6: Add a Message
echo "6. Testing POST /api/messages/add"
echo "   Request: curl -X POST ${BASE_URL}/api/messages/add -d 'from=user1&to=user2&message=Hello from test script'"
curl -s -X POST "${BASE_URL}/api/messages/add" \
  -d "from=user1&to=user2&message=Hello from test script"
echo ""
echo ""

# Test 7: Get Messages Again (to verify addition)
echo "7. Verifying message addition"
echo "   Request: curl ${BASE_URL}/api/messages"
curl -s "${BASE_URL}/api/messages" | python3 -m json.tool
echo ""
echo ""

# Test 8: Get Camera Image
echo "8. Testing GET /camera (saving to test_camera.jpg)"
echo "   Request: curl ${BASE_URL}/camera -o test_camera.jpg"
curl -s "${BASE_URL}/camera" -o test_camera.jpg
if [ -f test_camera.jpg ]; then
    SIZE=$(stat -f%z test_camera.jpg 2>/dev/null || stat -c%s test_camera.jpg 2>/dev/null)
    echo "   Camera image saved: test_camera.jpg (${SIZE} bytes)"
else
    echo "   Failed to save camera image"
fi
echo ""
echo ""

# Test 9: Get Updated Statistics
echo "9. Testing GET /api/stats (after additions)"
echo "   Request: curl ${BASE_URL}/api/stats"
curl -s "${BASE_URL}/api/stats" | python3 -m json.tool
echo ""
echo ""

echo "================================"
echo "API Testing Complete!"
echo "================================"
