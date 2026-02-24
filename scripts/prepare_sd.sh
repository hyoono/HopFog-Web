#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# prepare_sd.sh — Copy web UI files to an SD card for HopFog-Web ESP32
# ──────────────────────────────────────────────────────────────────────
#
# Usage:
#   ./scripts/prepare_sd.sh /path/to/sd/mount
#
# Example (Linux):   ./scripts/prepare_sd.sh /media/$USER/SD_CARD
# Example (macOS):   ./scripts/prepare_sd.sh /Volumes/SD_CARD
# Example (WSL):     ./scripts/prepare_sd.sh /mnt/d
#
# This copies the data/sd/ contents to the SD card root and creates the
# db/ directory so the ESP32 can store JSON data on first boot.
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SD_SOURCE="$PROJECT_ROOT/data/sd"

# ── Check arguments ─────────────────────────────────────────────────
if [ $# -lt 1 ]; then
    echo -e "${RED}Error:${NC} Please provide the SD card mount point."
    echo ""
    echo "Usage: $0 /path/to/sd/mount"
    echo ""
    echo "Examples:"
    echo "  Linux:  $0 /media/\$USER/SD_CARD"
    echo "  macOS:  $0 /Volumes/SD_CARD"
    echo "  WSL:    $0 /mnt/d"
    exit 1
fi

SD_MOUNT="$1"

# ── Validate mount point ────────────────────────────────────────────
if [ ! -d "$SD_MOUNT" ]; then
    echo -e "${RED}Error:${NC} '$SD_MOUNT' is not a valid directory."
    echo "Make sure the SD card is inserted and mounted."
    exit 1
fi

# ── Validate source exists ──────────────────────────────────────────
if [ ! -d "$SD_SOURCE/www" ]; then
    echo -e "${RED}Error:${NC} Source directory '$SD_SOURCE/www' not found."
    echo "Are you running this from the project root?"
    exit 1
fi

# ── Confirm ─────────────────────────────────────────────────────────
echo -e "${YELLOW}HopFog-Web — SD Card Preparation${NC}"
echo "──────────────────────────────────"
echo "Source:      $SD_SOURCE"
echo "Destination: $SD_MOUNT"
echo ""
echo "This will copy web files to the SD card."
read -rp "Continue? [y/N] " answer
if [[ ! "$answer" =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 0
fi

# ── Copy files ──────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}Copying web files...${NC}"
cp -rv "$SD_SOURCE/www" "$SD_MOUNT/"

echo ""
echo -e "${GREEN}Creating db/ directory...${NC}"
mkdir -p "$SD_MOUNT/db"

# ── Summary ─────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}✓ SD card prepared successfully!${NC}"
echo ""
echo "SD card contents:"
find "$SD_MOUNT" -maxdepth 3 -not -path '*/\.*' | head -30
echo ""
echo "Next steps:"
echo "  1. Safely eject the SD card"
echo "  2. Insert it into the ESP32 SD card module"
echo "  3. Flash the firmware: ./scripts/deploy.sh"
