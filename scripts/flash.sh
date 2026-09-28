#!/usr/bin/env bash
# Flashes an already-built firmware.bin from the HOST, using esptool
# directly (`pip install esptool` — a small pure-Python package, not the
# full ESP-IDF toolchain). This is the portable path: Docker Desktop on
# macOS can't pass USB devices into containers at all, and doing so on
# Windows needs extra usbipd-win setup (see README) — flashing from the
# host with esptool works the same way on every OS.
#
# Usage: scripts/flash.sh <PORT>
#   e.g. scripts/flash.sh /dev/ttyUSB0
#        scripts/flash.sh COM5
set -euo pipefail
cd "$(dirname "$0")/../firmware/build"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <PORT>  (e.g. /dev/ttyUSB0 or COM5)" >&2
    exit 1
fi
PORT="$1"

if [ ! -f flash_args ]; then
    echo "firmware/build/flash_args not found — run scripts/build.sh first." >&2
    exit 1
fi

# cwd must be firmware/build/: flash_args lists binary paths relative to it.
python3 -m esptool --chip esp32s3 -p "$PORT" -b 460800 write_flash @flash_args
