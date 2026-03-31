#!/bin/bash
# HopFog Health Check Script

APP_URL="${1:-http://localhost:8000}"
HEALTH_ENDPOINT="$APP_URL/health"
SERVICE_NAME="hopfog"

# Check if service is running
if systemctl is-active --quiet $SERVICE_NAME 2>/dev/null; then
    echo "✓ Service is running"
else
    echo "✗ Service is not running"
    exit 1
fi

# Check HTTP endpoint
response=$(curl -s -o /dev/null -w "%{http_code}" --max-time 10 $HEALTH_ENDPOINT 2>/dev/null || echo "000")

if [ "$response" = "200" ]; then
    echo "✓ Health endpoint responding (HTTP $response)"
else
    echo "✗ Health endpoint not responding (HTTP $response)"
    exit 1
fi

echo "All checks passed"
