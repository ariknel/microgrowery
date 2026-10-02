# Same pattern as the hub firmware's Dockerfile — thin wrapper around
# Espressif's official ESP-IDF image. Builds the firmware; doesn't run it.
# The compiled .bin flashes onto and runs on the ESP32-CAM's own silicon.

ARG IDF_VERSION=v5.2.2
FROM espressif/idf:${IDF_VERSION}

WORKDIR /project

# Inherits the base image's own entrypoint (sources export.sh so idf.py is
# on PATH) and its default CMD.
