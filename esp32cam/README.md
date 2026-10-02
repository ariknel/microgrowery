# GrowBox Camera Firmware

ESP-IDF firmware (bare C, no Arduino) for the common AI-Thinker ESP32-CAM
board — the camera half of the [GrowBox](../README.md) build. Talks to the
hub over a dedicated UART link; the hub proxies the stream to the
dashboard. Same build method as the hub firmware: Docker, no local
toolchain install needed.

## What it does

1. Connects to WiFi (same network as the hub) and announces its IP over
   UART: `{"cam_ip":"x.x.x.x"}\n`. The hub only uses this as a liveness
   signal — the actual video never leaves UART, so the IP value itself
   isn't used for routing.
2. Listens on that same UART for commands from the hub:
   - `{"cmd":"stream","state":1}` — start streaming (~3fps QVGA JPEG)
   - `{"cmd":"stream","state":0}` — stop
   - `{"cmd":"snapshot"}` — capture and send exactly one frame
3. Sends each JPEG frame framed as the hub expects:
   `0xFF 0xD8 [4-byte big-endian length] [JPEG bytes] 0xFF 0xD9`

No local web server, no SD card, no direct connection from the browser —
the hub is the only thing this board talks to.

## Hardware

**UART link to the hub** (separate from the programming UART):

| Signal | GPIO | Baud |
|--------|------|------|
| TX | 14 | 921600 |
| RX | 15 | 921600 |

GPIO1/GPIO3 (UART0) are *not* used for this link — those are only touched
by the external USB-serial programmer during the one-time flash. Wire
GPIO14/15 to the hub's UART1 (GPIO17/18) permanently; the programmer gets
unplugged after flashing and isn't part of the running system.

**Camera pins** — standard AI-Thinker mapping, see `main/camera_pins.h`.
Double-check against your board's silkscreen if you're on a less common
clone; pin assignment is the single most common source of
`esp_camera_init()` failures.

## Flashing (one-time, needs the external programmer)

AI-Thinker boards have no native USB. You need a USB-to-serial adapter
(FTDI, CP2102, etc.) wired to the board's UART0 (GPIO1 TX, GPIO3 RX, 5V,
GND), **plus GPIO0 pulled to GND** to enter download mode:

1. Wire the adapter: adapter TX → board RX (GPIO3), adapter RX → board TX
   (GPIO1), 5V, GND.
2. Bridge GPIO0 to GND (jumper wire or button, if your board has one).
3. Power-cycle or press reset — the board is now in download mode.
4. Flash (see below).
5. Remove the GPIO0-to-GND bridge, power-cycle again — it boots normally.
6. Unplug the external programmer. From here on the board only talks to
   the hub over GPIO14/15.

## Configure WiFi

Set the SSID/password before building — same network as the hub:

```bash
docker compose run --rm menuconfig
# → GrowBox Camera Configuration → WiFi SSID / WiFi Password
```

(Or edit `sdkconfig.defaults` directly before the first build.)

## Build

```bash
docker compose run --rm build
# or:
scripts/build.sh
```

First build needs internet access once — `main/idf_component.yml` pulls
in Espressif's `esp32-camera` component from the registry, which isn't
bundled with core ESP-IDF.

## Flash

Recommended: from the host with plain esptool (`pip install esptool`),
after wiring up as described above:

```bash
scripts/flash.sh /dev/ttyUSB0     # or COM5 on Windows
```

Or through Docker with USB passthrough set up (see the hub firmware's
README for the per-OS notes — same deal here):

```bash
docker compose run --rm flash
```

## Monitor

```bash
scripts/monitor.sh /dev/ttyUSB0
# or, with USB passthrough:
docker compose run --rm monitor
```

## VS Code dev container

Same pattern as the hub: open this folder in VS Code and accept "Reopen in
Container" — `idf.py build` then works straight from the integrated
terminal, no local toolchain needed. The container deliberately has no USB
device access (a hard-coded device mount breaks container creation outright
on Windows, where Docker Desktop has no real `/dev` to bind from), so flash
and monitor from the **host** instead, same as above
(`scripts/flash.sh` / `scripts/monitor.sh`), or from a separate terminal —
not the devcontainer's own.

## Known gaps

- Pin mapping is the standard AI-Thinker layout, transcribed from the
  widely-used reference config — not yet bench-tested against real
  hardware.
- No flash-LED control (GPIO4) — left unused since it doubles as an SD
  card data line on this board and SD isn't used here anyway.
- Frame rate (~3fps) is a fixed delay between captures, not adaptive to
  actual encode time — fine for a grow-cabinet monitor, not tuned for
  anything faster.
idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/growbox_cam.bin
or from the "/workspaces/microgrowery/esp32cam/build" directory
 python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
root@1da86854c589:/workspaces/microgrowery/esp32cam# 