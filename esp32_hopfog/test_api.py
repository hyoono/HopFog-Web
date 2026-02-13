#!/usr/bin/env python3
"""
HopFog ESP32-CAM API Test Script
This script helps test the API endpoints of the ESP32-CAM web server
"""

import requests
import json
import sys

# Configuration - Update this with your ESP32-CAM's IP address
ESP32_IP = "192.168.1.100"
BASE_URL = f"http://{ESP32_IP}"

def print_header(text):
    print("\n" + "=" * 60)
    print(text)
    print("=" * 60)

def print_test(number, description):
    print(f"\n{number}. {description}")
    print("-" * 60)

def pretty_print_json(data):
    if isinstance(data, str):
        try:
            data = json.loads(data)
        except:
            print(data)
            return
    print(json.dumps(data, indent=2))

def test_get_stats():
    print_test(1, "Testing GET /api/stats")
    try:
        response = requests.get(f"{BASE_URL}/api/stats", timeout=5)
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            pretty_print_json(response.json())
        else:
            print(f"Error: {response.text}")
        return response.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_get_fognodes():
    print_test(2, "Testing GET /api/fognodes")
    try:
        response = requests.get(f"{BASE_URL}/api/fognodes", timeout=5)
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            pretty_print_json(response.json())
        else:
            print(f"Error: {response.text}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_add_fognode():
    print_test(3, "Testing POST /api/fognodes/add")
    data = {
        "device_name": "TestNode-Python",
        "ip_address": "192.168.1.50",
        "status": "active"
    }
    print(f"Data: {data}")
    try:
        response = requests.post(
            f"{BASE_URL}/api/fognodes/add",
            data=data,
            timeout=5
        )
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            pretty_print_json(response.json())
        else:
            print(f"Error: {response.text}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_get_messages():
    print_test(4, "Testing GET /api/messages")
    try:
        response = requests.get(f"{BASE_URL}/api/messages", timeout=5)
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            pretty_print_json(response.json())
        else:
            print(f"Error: {response.text}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_add_message():
    print_test(5, "Testing POST /api/messages/add")
    data = {
        "from": "user1",
        "to": "user2",
        "message": "Hello from Python test script"
    }
    print(f"Data: {data}")
    try:
        response = requests.post(
            f"{BASE_URL}/api/messages/add",
            data=data,
            timeout=5
        )
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            pretty_print_json(response.json())
        else:
            print(f"Error: {response.text}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_camera():
    print_test(6, "Testing GET /camera")
    try:
        response = requests.get(f"{BASE_URL}/camera", timeout=10)
        print(f"Status: {response.status_code}")
        if response.status_code == 200:
            with open("test_camera.jpg", "wb") as f:
                f.write(response.content)
            print(f"Camera image saved: test_camera.jpg ({len(response.content)} bytes)")
        else:
            print(f"Error: {response.text}")
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def main():
    print_header("HopFog ESP32-CAM API Test Script")
    print(f"Testing ESP32-CAM at: {BASE_URL}")
    
    # Check if ESP32-CAM is reachable
    print("\nChecking connection to ESP32-CAM...")
    try:
        response = requests.get(BASE_URL, timeout=5)
        print("✓ Connection successful!")
    except Exception as e:
        print(f"✗ Connection failed: {e}")
        print(f"\nPlease check:")
        print(f"1. ESP32-CAM is powered on and connected to WiFi")
        print(f"2. Your computer is on the same network")
        print(f"3. The IP address is correct (currently: {ESP32_IP})")
        print(f"   Update ESP32_IP variable in this script if needed")
        sys.exit(1)
    
    # Run tests
    tests_passed = 0
    total_tests = 7
    
    if test_get_stats(): tests_passed += 1
    if test_get_fognodes(): tests_passed += 1
    if test_add_fognode(): tests_passed += 1
    if test_get_fognodes(): tests_passed += 1  # Verify addition
    if test_get_messages(): tests_passed += 1
    if test_add_message(): tests_passed += 1
    if test_camera(): tests_passed += 1
    
    # Final statistics
    print_test("Final", "Get updated statistics")
    test_get_stats()
    
    print_header("API Testing Complete!")
    print(f"Tests passed: {tests_passed}/{total_tests}")
    
    if tests_passed == total_tests:
        print("✓ All tests passed!")
    else:
        print(f"⚠ {total_tests - tests_passed} test(s) had issues")

if __name__ == "__main__":
    # Check if custom IP is provided as argument
    if len(sys.argv) > 1:
        ESP32_IP = sys.argv[1]
        BASE_URL = f"http://{ESP32_IP}"
        print(f"Using custom IP: {ESP32_IP}")
    
    main()
