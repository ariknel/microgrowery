# Plain arm-none-eabi-gcc + make toolchain for the CH32F4P6 fan controller
# board. WCH doesn't publish an official Docker build image (unlike
# Espressif's espressif/idf, used by ../firmware and ../esp32cam), so this
# builds a minimal one from a plain Debian base — just the compiler and
# make, nothing WCH-specific baked in.
#
# The WCH EVT peripheral library itself is NOT bundled in this image — it's
# distributed by WCH as a zip from their own site, not a package registry
# reachable from a Dockerfile, so you supply it yourself under Libraries/
# (see README.md). This image only provides the compiler that builds
# against it.
#
# IMPORTANT: this image builds the firmware (an .elf/.hex). The compiled
# firmware is what flashes onto and runs on the CH32F4P6 — nothing from
# this container ever runs on the chip itself. Flashing happens on the
# host via a WCH-LinkE programmer, not through this container (WCH-LinkE's
# host tools aren't containerized here — see README.md).

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /project
