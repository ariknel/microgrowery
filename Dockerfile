# Thin wrapper around Espressif's official ESP-IDF image. This exists so
# `docker build .` gives you a pinned, reproducible toolchain image if you
# want one (e.g. for CI); for day-to-day local builds, docker-compose.yml
# just uses the same base image directly with a volume mount, which is
# simpler and doesn't require rebuilding an image every time firmware
# source changes.
#
# IMPORTANT: this image builds the firmware. The compiled .bin is what
# gets flashed onto and runs on the ESP32-S3 — nothing from this
# container ever runs on the device itself; there's no OS on the chip
# for a container to sit on.

ARG IDF_VERSION=v5.2.2
FROM espressif/idf:${IDF_VERSION}

WORKDIR /project

# Inherits the base image's own entrypoint (which sources export.sh so
# `idf.py` is on PATH) and its default CMD.
