#!/usr/bin/env bash
# Flashes an already-built firmware.bin from the HOST using esptool
# directly (`pip install esptool`), sidestepping Docker USB passthrough.
# Only needed for the one-time flash — plug in the external USB-serial
# programmer (AI-Thinker boards have no native USB), hold/wire GPIO0 low
# to enter download mode, flash, then disconnect it; the board talks to
# the hub over its own separate UART (GPIO14/15) afterward.
#
# Usage: scripts/flash.sh <PORT>
set -euo pipefail
cd "$(dirname "$0")/../build"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <PORT>  (e.g. /dev/ttyUSB0 or COM5)" >&2
    exit 1
fi
PORT="$1"

if [ ! -f flash_args ]; then
    echo "build/flash_args not found — run scripts/build.sh first." >&2
    exit 1
fi

# cwd must be build/: flash_args lists binary paths relative to it.
python3 -m esptool --chip esp32 -p "$PORT" -b 460800 write_flash @flash_args
