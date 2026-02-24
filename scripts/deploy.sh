#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# deploy.sh — Build and flash HopFog-Web firmware to ESP32
# ──────────────────────────────────────────────────────────────────────
#
# Usage:
#   ./scripts/deploy.sh              # Build + flash + monitor
#   ./scripts/deploy.sh build        # Build only
#   ./scripts/deploy.sh flash        # Flash only (must build first)
#   ./scripts/deploy.sh monitor      # Open serial monitor only
#   ./scripts/deploy.sh all          # Build + flash + monitor (default)
#
# Options:
#   --port /dev/ttyUSB0     Specify serial port (auto-detected if omitted)
#   --baud 115200           Specify monitor baud rate (default: 115200)
#
# Prerequisites:
#   - PlatformIO CLI: pip install platformio
#   - ESP32 connected via USB
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ACTION="${1:-all}"
PORT=""
BAUD="115200"

# ── Parse optional flags ────────────────────────────────────────────
shift || true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --baud) BAUD="$2"; shift 2 ;;
        *) echo -e "${RED}Unknown option: $1${NC}"; exit 1 ;;
    esac
done

# ── Check PlatformIO ────────────────────────────────────────────────
check_pio() {
    if ! command -v pio &> /dev/null; then
        echo -e "${RED}Error:${NC} PlatformIO CLI not found."
        echo ""
        echo "Install it with:"
        echo "  pip install platformio"
        echo ""
        echo "Or install the VS Code PlatformIO extension."
        exit 1
    fi
    echo -e "${GREEN}✓${NC} PlatformIO found: $(pio --version)"
}

# ── Build ────────────────────────────────────────────────────────────
do_build() {
    echo ""
    echo -e "${CYAN}═══ Building firmware ═══${NC}"
    cd "$PROJECT_ROOT"
    pio run
    echo -e "${GREEN}✓ Build complete${NC}"
}

# ── Flash ────────────────────────────────────────────────────────────
do_flash() {
    echo ""
    echo -e "${CYAN}═══ Flashing to ESP32 ═══${NC}"
    cd "$PROJECT_ROOT"
    local port_flag=""
    if [ -n "$PORT" ]; then
        port_flag="--upload-port $PORT"
    fi
    pio run --target upload $port_flag
    echo -e "${GREEN}✓ Flash complete${NC}"
}

# ── Monitor ──────────────────────────────────────────────────────────
do_monitor() {
    echo ""
    echo -e "${CYAN}═══ Serial Monitor (${BAUD} baud) ═══${NC}"
    echo -e "${YELLOW}Press Ctrl+C to exit${NC}"
    cd "$PROJECT_ROOT"
    local port_flag=""
    if [ -n "$PORT" ]; then
        port_flag="--port $PORT"
    fi
    pio device monitor --baud "$BAUD" $port_flag
}

# ── Main ─────────────────────────────────────────────────────────────
echo -e "${YELLOW}HopFog-Web — ESP32 Deployment${NC}"
echo "──────────────────────────────"

check_pio

case "$ACTION" in
    build)
        do_build
        ;;
    flash)
        do_flash
        ;;
    monitor)
        do_monitor
        ;;
    all)
        do_build
        do_flash
        echo ""
        echo -e "${GREEN}═══ Deployment complete! ═══${NC}"
        echo ""
        echo "Opening serial monitor..."
        echo "Watch for the ESP32's IP address in the output."
        echo ""
        do_monitor
        ;;
    *)
        echo -e "${RED}Unknown action: $ACTION${NC}"
        echo "Usage: $0 [build|flash|monitor|all]"
        exit 1
        ;;
esac
