#!/usr/bin/env bash
# Builds the firmware inside the official ESP-IDF Docker image.
# Output lands in firmware/build/ on the host (it's a bind mount).
set -euo pipefail
cd "$(dirname "$0")/.."
docker compose run --rm build
