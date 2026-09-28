#!/usr/bin/env bash
# Plain serial monitor from the host (pyserial's miniterm — `pip install
# pyserial`). No IDF backtrace decoding, but works everywhere without any
# Docker USB passthrough setup. For the full idf.py monitor experience,
# use `docker compose run --rm monitor` instead (see README for the
# per-OS USB passthrough it needs).
#
# Usage: scripts/monitor.sh <PORT>
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <PORT>  (e.g. /dev/ttyUSB0 or COM5)" >&2
    exit 1
fi

python3 -m serial.tools.miniterm "$1" 115200
