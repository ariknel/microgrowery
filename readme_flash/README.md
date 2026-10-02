<<<<<<< HEAD
# Microgrowery Controller

ESP32-S3 firmware for a DIY grow-cabinet controller (USB-PD power intake,
LED + fan control, light schedule, AHT20 temp/humidity, ESP32-CAM feed),
paired with the dashboard in `dashboard.html`.

**What Docker does and doesn't do here:** it builds the firmware in a
reproducible container (Espressif's official `espressif/idf` image) so
you don't need the ESP-IDF toolchain installed on your machine. It does
**not** run the firmware — there's no OS on the ESP32-S3 for a container
to sit on. The container produces a `.bin`; that binary is what gets
flashed onto and runs directly on the chip's own silicon.

## Layout

```
firmware/           ESP-IDF project (this is what gets built/flashed)
  main/              all C sources, plus dashboard.html embedded into the binary
  partitions.csv     custom partition table for the 8MB flash
  sdkconfig.defaults  target/flash/console defaults
.devcontainer/       VS Code dev container config (recommended workflow)
Dockerfile           image the dev container and docker-compose both use
docker-compose.yml   alternative to the dev container, for use outside VS Code
scripts/             host-side convenience wrappers (used by the alternative path)
dashboard.html       the browser dashboard (also copied into firmware/main/)
```

If you edit `dashboard.html` at the repo root, re-copy it into
`firmware/main/dashboard.html` before rebuilding — it's embedded into the
binary at build time via `EMBED_TXTFILES`, not read from disk at runtime.

---

## One-time setup

1. Install **Docker Desktop**, with the **WSL2** backend enabled
   (Settings → General → "Use the WSL 2 based engine"). This is the
   default on a fresh install.
2. Install the **Dev Containers** extension in VS Code
   (`ms-vscode-remote.remote-containers`).
3. **Windows only** — the ESP32-S3 shows up as a USB device to Windows,
   but Docker Desktop's containers run inside WSL2, which doesn't see
   Windows USB devices by default. [usbipd-win](https://github.com/dorssel/usbipd-win)
   bridges that gap, once per device:
   ```powershell
   winget install usbipd
   ```
   Leave this step here for now — you'll run the actual attach command
   in step 2 below, *after* plugging the board in.

That's the whole one-time setup. Everything below is the normal
day-to-day loop.

---

## Everyday workflow

### 1. Plug in the board

Connect the ESP32-S3 to your PC over USB.

**Windows only** — share it into WSL2 (repeat this each time you
replug the board or reboot Windows; it does *not* persist):
```powershell
usbipd list
# find the line for your board, note its BUSID, e.g. 1-4
usbipd bind --busid 1-4
usbipd attach --wsl --busid 1-4
```

Linux and macOS: nothing to do here, skip to step 2.

### 2. Open the project in VS Code

Open the repo root folder in VS Code. A popup appears in the
bottom-right: **"Reopen in Container"** — click it.

*(First time only: this builds the image, which takes a few minutes.
Every time after that, it reuses the cached image and starts in a few
seconds. If you miss the popup, open the Command Palette —
`Ctrl+Shift+P` — and run **Dev Containers: Reopen in Container**.)*

Once it's open, VS Code's integrated terminal is now a shell *inside*
the container, already sitting in `firmware/`, with `idf.py` on PATH
and the board's serial port visible. This is the only terminal you
need from here on — no more `docker compose run`, no separate WSL
window.

### 3. Build

```
idf.py build
```

### 4. Flash

Find the port name first (only needed once per session — it won't
change until you unplug/replug):
```
ls /dev/ttyUSB* /dev/ttyACM*
```
Then:
```
idf.py -p /dev/ttyUSB0 flash
```
(swap in whichever port `ls` showed you.)
idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/growbox_cam.bin
or from the "/workspaces/microgrowery/esp32cam/build" directory
 python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
root@1da86854c589:/workspaces/microgrowery/esp32cam# 
### 5. Monitor

```
idf.py -p /dev/ttyUSB0 monitor
```
Ctrl+] exits the monitor.

### 6. The rebuild loop

Once you've done steps 3–5 once, day-to-day is just:
```
idf.py build flash monitor
```
— builds, flashes, and drops straight into the serial monitor in one
command, all from the same VS Code terminal.

---

## Alternative: without VS Code

If you'd rather not use the dev container (scripted builds, CI, a
different editor), `docker-compose.yml` does the same build in one shot
from any terminal:

```
docker compose run --rm build
```

Flashing this way needs the same USB passthrough as above, and Docker
Desktop's device passthrough has real per-OS limits:

- **Linux**: works directly — `docker compose run --rm flash`
  (set `PORT=/dev/ttyACM0` etc. if it's not `/dev/ttyUSB0`).
- **Windows**: do the `usbipd attach` step above from a WSL2 shell, then
  `PORT=/dev/ttyUSB0 docker compose run --rm flash` from that same shell.
- **macOS**: not possible — Docker Desktop on Mac has no generic USB
  passthrough. Use `scripts/flash.sh` instead (below).

For macOS, or anytime you don't want to deal with passthrough at all,
flash from the host directly with plain `esptool` (`pip install esptool`
— a small pure-Python package, not the full toolchain):
```
scripts/flash.sh /dev/ttyUSB0     # Linux/macOS
scripts/flash.sh COM5             # Windows, from PowerShell
```
And monitor with `pyserial` (`pip install pyserial`):
```
scripts/monitor.sh /dev/ttyUSB0
=======
<p align="center">
  <img src="docs/images/growbox_hero.jpg" alt="GrowBox Cabinet" width="800"/>
</p>

<h1 align="center">🌱 GrowBox</h1>

<p align="center">
  Open-source USB-C powered smart microgrowery controller.<br/>
  ESP32-S3 hub · 65W LED panel · WiFi dashboard · Camera streaming · USB-C PD
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.x-blue"/>
  <img src="https://img.shields.io/badge/language-C-lightgrey"/>
  <img src="https://img.shields.io/badge/power-USB--C%20PD%2020V-green"/>
  <img src="https://img.shields.io/badge/license-MIT-brightgreen"/>
</p>

---

## Table of Contents

- [Overview](#overview)
- [Cabinet](#cabinet)
- [System Architecture](#system-architecture)
- [PCB Boards](#pcb-boards)
  - [1. Main Hub Board](#1-main-hub-board)
  - [2. LED Driver Board](#2-led-driver-board)
  - [3. LED Panel Board](#3-led-panel-board)
  - [4. Sensor Board](#4-sensor-board)
- [Web Dashboard](#web-dashboard)
- [Camera Feed](#camera-feed)
- [Wiring Overview](#wiring-overview)
- [Features](#features)
- [API Endpoints](#api-endpoints)
- [Project Structure](#project-structure)
- [FreeRTOS Task Structure](#freertos-task-structure)
- [NVS Configuration](#nvs-configuration)
- [Building and Flashing](#building-and-flashing)
- [ESP32-CAM Setup](#esp32-cam-setup)
- [Grow Schedule Recommendations](#grow-schedule-recommendations)
- [Bill of Materials](#bill-of-materials)
- [License](#license)

---

## Overview

GrowBox is a fully custom USB-C powered microgrowery controller designed to fit under a desk. It drives a 65W warm white LED grow panel, controls intake and exhaust ventilation fans, monitors temperature and humidity, streams a live camera feed, and serves a local web dashboard — all from a single USB-C cable.

Everything is custom designed: four separate PCBs, ESP32-S3 firmware in bare C using ESP-IDF, and a self-contained HTML dashboard served directly from the microcontroller.

<p align="center">
  <img src="docs/images/growbox_open_door.jpg" alt="Cabinet open showing plants" width="700"/>
</p>

**Key specs at a glance:**

| Parameter | Value |
|-----------|-------|
| Input power | USB-C PD 20V via FUSB302 |
| Max LED power | 65W (60× warm white LEDs) |
| LED panel size | 4× 85×55mm boards, 2×2 arrangement |
| Cabinet size | 380×380×850mm (external) |
| MCU | ESP32-S3 bare SoC, 8MB NOR flash |
| Connectivity | WiFi 802.11 b/g/n, mDNS growbox.local |
| Environment sensor | AHT20 temperature + humidity |
| Camera | ESP32-CAM MJPEG stream |
| Fan control | 2× PWM fans, independent speed |

---

## Cabinet

The cabinet is constructed from 18mm MDF with a full-front door sealed with EPDM weatherstripping and magnetic latches. Internal walls are lined with reflective Mylar film to maximize light distribution. A DIY activated carbon filter with a 120mm exhaust fan handles odor control.

<p align="center">
  <img src="docs/images/cabinet_front_closed.jpg" alt="Cabinet front closed" width="400"/>
  &nbsp;&nbsp;
  <img src="docs/images/cabinet_front_open.jpg" alt="Cabinet front open" width="400"/>
</p>

<p align="center">
  <img src="docs/images/cabinet_interior_top.jpg" alt="Interior top view showing LED panel" width="400"/>
  &nbsp;&nbsp;
  <img src="docs/images/cabinet_interior_side.jpg" alt="Interior side view showing fans and filter" width="400"/>
</p>

**Cabinet dimensions:**

```
External:   380 × 380 × 850mm  (W × D × H)
Internal:   342 × 342 × 814mm
Wall:       18mm MDF

LED panel:  mounted at top, 10mm clearance minimum to plants
Usable grow height: ~500mm (accounting for pot + light clearance)

Fan intake:  120mm, bottom-right panel
Fan exhaust: 120mm, top-right panel
Carbon filter: 3D printed housing, 80mm depth activated carbon bed,
               mounted inline on exhaust
```

<p align="center">
  <img src="docs/images/cabinet_dimensions.jpg" alt="Cabinet technical drawing" width="600"/>
</p>

---

## System Architecture

```
                        USB-C PD Charger (100W GaN)
                                  │
                                  │ 20V @ up to 5A
                                  ▼
                    ┌─────────────────────────────┐
                    │        MAIN HUB BOARD        │
                    │                              │
                    │  FUSB302 ──► 20V rail        │
                    │  ESP32-S3                    │
                    │  SY8120B ──► 3.3V logic      │
                    │  AHT20 sensor board (ext.)   │
                    │  ESP32-CAM (ext. via UART)   │
                    └──────────┬───────────────────┘
                               │
               ┌───────────────┼───────────────┐
               │               │               │
               ▼               ▼               ▼
        LED Board 1      LED Board 2     LED Board 3+4
       (3× TX6120)      (3× TX6120)     (3× TX6120 each)
       15 LEDs           15 LEDs         15 LEDs each
       ~16W              ~16W            ~16W each
```

All four LED driver boards receive:
- **20V power** via individual JST VH 2-pin cables (star topology from hub)
- **DIM_PWM signal** via 2-pin signal cable (daisy-chainable, milliamp level)

---

## PCB Boards

The project consists of four custom PCBs, all designed in KiCad and manufactured at JLCPCB.

---

### 1. Main Hub Board

> The brain of the system. Houses the ESP32-S3 bare SoC, FUSB302 PD negotiation, all power regulation, and JST connectors for every other board and peripheral.

<p align="center">
  <img src="docs/images/pcb_hub_top.jpg" alt="Hub board top" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_hub_bottom.jpg" alt="Hub board bottom" width="500"/>
</p>

<p align="center">
  <img src="docs/images/pcb_hub_render_front.jpg" alt="Hub board KiCad 3D render front" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_hub_render_back.jpg" alt="Hub board KiCad 3D render back" width="500"/>
</p>

**Key components:**

| Component | Part | Description |
|-----------|------|-------------|
| MCU | ESP32-S3 QFN-56 | Dual-core 240MHz, WiFi+BLE, USB JTAG |
| Flash | GD25WQ64ESIGR | 8MB NOR SPI flash, WSON-8 |
| PD controller | FUSB302UCX | USB-C PD sink, I2C controlled |
| 3.3V regulator | SY8120B | 26V input, 1A buck |
| USB-C receptacle | 16-pin SMD | Power + CC1/CC2 for FUSB302 + D+/D- for JTAG |
| Crystal | 40MHz ±10ppm | Required for bare SoC |

**Connectors on hub board:**

| Connector | Type | Connects to |
|-----------|------|------------|
| J1 | USB-C 16P | Power input + JTAG programming |
| J2–J5 | JST VH 2-pin | 20V power to each LED driver board |
| J6–J9 | JST XH 2-pin | DIM_PWM signal to each LED driver board |
| J10 | JST XH 4-pin | I2C to sensor board (SDA, SCL, 3.3V, GND) |
| J11 | JST XH 4-pin | UART to ESP32-CAM (TX, RX, 3.3V, GND) |
| J12 | JST XH 2-pin | Intake fan |
| J13 | JST XH 2-pin | Exhaust fan |
| J14 | 2-pin header | UART0 debug (TXD, RXD) |

**GPIO assignment:**

| GPIO | Function |
|------|---------|
| GPIO4 | SPI MOSI (TFT) |
| GPIO5 | SPI MISO (TFT) |
| GPIO6 | SPI CLK (TFT) |
| GPIO7 | TFT CS |
| GPIO8 | I2C SDA |
| GPIO9 | I2C SCL |
| GPIO10 | FUSB302 INT# |
| GPIO11 | LED DIM PWM |
| GPIO12 | Fan 1 PWM |
| GPIO13 | Fan 2 PWM |
| GPIO14 | TFT DC |
| GPIO17 | UART1 RX (camera) |
| GPIO18 | UART1 TX (camera) |
| GPIO19 | USB D- (JTAG) |
| GPIO20 | USB D+ (JTAG) |
| GPIO21 | TFT RST |
| GPIO38 | TFT backlight |
| GPIO39 | Touch CS |
| GPIO40 | Touch IRQ |
| GPIO41 | SD card CS |

---

### 2. LED Driver Board

> One of four identical driver boards. Each carries 3× TX6120 buck constant-current LED drivers, powering 3 strings of 5 LEDs each (15 LEDs, ~16W per board).

<p align="center">
  <img src="docs/images/pcb_driver_top.jpg" alt="Driver board top assembled" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_driver_bottom.jpg" alt="Driver board bottom" width="500"/>
</p>

<p align="center">
  <img src="docs/driver_front.PNG" alt="Driver board KiCad 3D render" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/driver_back.PNG" alt="Driver board KiCad 3D render back" width="500"/>
</p>

<p align="center">
  <img src="docs/images/pcb_driver_array.jpg" alt="All 4 driver boards side by side" width="700"/>
</p>

**Per-channel design (×3 per board):**

| Component | Value | Purpose |
|-----------|-------|---------|
| TX6120 | ESOP-8 | Buck CC LED driver, up to 100V, 1.5A |
| L1 | 330µH, Isat ≥500mA, shielded | Flyback inductor |
| D1 | SS14 (1A 40V SOD-123) | Freewheeling Schottky |
| R1 | 7.2kΩ 1% 0805 | VDD supply resistor |
| R2 | 0.75Ω 1% 0603 | Current sense — sets 340mA per string |
| C1 | 10µF 25V X7R 0805 | Input decoupling |
| C2 | 10nF 0402 | VDD bypass |
| C3 | 10nF 0402 | DIM pin filter |

**Current calculation:**

```
I_LED = Vcs / R2 = 0.255V / 0.75Ω = 340mA per string
Power per string = 5 LEDs × 3.2V × 0.340A = 5.44W
Power per board  = 3 strings × 5.44W = 16.3W
Total (4 boards) = 4 × 16.3W = 65.3W at 100% PWM
```

**Connectors:**

| Connector | Type | Signal |
|-----------|------|--------|
| J1 | JST VH 2-pin | 20V power in from hub |
| J2 | JST XH 2-pin | DIM_PWM + GND from hub |
| J3–J11 | JST XH 2-pin | LED+ and LED- per string (3 strings out) |

---

### 3. LED Panel Board

> One of four identical LED boards. Each carries 15× XL-3030WWC-1W-3V warm white LEDs in a 3×5 grid, mounted on FR4 with thermal vias under every LED pad and a B.Cu copper pour on the back side for heat spreading. An aluminum plate is bolted to the back of each board with thermal paste.

<p align="center">
  <img src="docs/images/pcb_led_top_off.jpg" alt="LED board not powered" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_led_top_on.jpg" alt="LED board powered on" width="500"/>
</p>

<p align="center">
  <img src="docs/led_front.PNG" alt="LED board KiCad 3D render" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/led_back.PNG" alt="LED board back showing copper pour and aluminum plate" width="500"/>
</p>

<p align="center">
  <img src="docs/images/pcb_led_panel_assembled.jpg" alt="All 4 LED boards assembled in 2x2 arrangement" width="700"/>
</p>

**LED specifications:**

| Parameter | Value |
|-----------|-------|
| LED | XL-3030WWC-1W-3V (XINGLIGHT) |
| LCSC | C2843893 |
| Package | SMD-3030-2P |
| Forward voltage | 3.2V typical |
| Rated current | 350mA |
| Operating current | 340mA (98% rated) |
| Color temperature | 2800–3200K warm white |
| Count per board | 15 (3 strings × 5 series) |

**Thermal design:**

```
LED thermal pad
      │
  9× vias (0.3mm drill) per LED
      │
B.Cu copper pour (full board flood)
      │
Thermal paste (Shin-Etsu X-23 or equivalent)
      │
3mm aluminum plate (bolted through 4 corner M3 holes)
```

**Board dimensions:** 85 × 55mm, FR4 1.6mm, HASL finish

**Connectors:**

| Connector | Type | Signal |
|-----------|------|--------|
| J1–J3 | JST XH 2-pin | LED+ and LED- per string (3 strings in from driver board) |

---

### 4. Sensor Board

> A minimal standalone board carrying only the AHT20 temperature and humidity sensor and its required passives. Connects to the hub via a 4-pin I2C cable. Designed to be mounted away from heat sources — ideally at mid-height on the cabinet wall for accurate ambient readings.

<p align="center">
  <img src="docs/sensor_front.PNG" alt="Sensor board top" width="400"/>
  &nbsp;&nbsp;
  <img src="docs/sensor_back.PNG" alt="Sensor board KiCad 3D render" width="400"/>
</p>

**Components:**

| Component | Value | Purpose |
|-----------|-------|---------|
| AHT20 | LCC-8 | Temperature (±0.3°C) and humidity (±2%RH) |
| C1 | 100nF 0402 | VDD decoupling |
| C2 | 10µF 0805 | Bulk decoupling |
| R1 | 4.7kΩ 0402 | I2C SDA pull-up |
| R2 | 4.7kΩ 0402 | I2C SCL pull-up |
| J1 | JST XH 4-pin | SDA, SCL, 3.3V, GND to hub |

**Board dimensions:** 25 × 20mm, FR4 1.6mm

> Pull-up resistors are on the sensor board rather than the hub board so the sensor board is self-contained and plug-and-play on any I2C bus.

---

## Web Dashboard

The dashboard is a single self-contained HTML file served directly from ESP32-S3 flash. No internet connection required. Access it at `http://growbox.local` from any device on the same WiFi network.

<p align="center">
  <img src="docs/images/dashboard_full.jpg" alt="Full dashboard screenshot" width="700"/>
</p>

<p align="center">
  <img src="docs/images/dashboard_mobile.jpg" alt="Dashboard on mobile" width="300"/>
  &nbsp;&nbsp;
  <img src="docs/images/dashboard_camera.jpg" alt="Dashboard with camera feed active" width="300"/>
  &nbsp;&nbsp;
  <img src="docs/images/dashboard_schedule.jpg" alt="Dashboard light schedule panel" width="300"/>
</p>

**Dashboard panels:**

| Panel | Description |
|-------|-------------|
| Camera feed | Live MJPEG stream from ESP32-CAM, tap to enable/disable |
| Temperature | Live °C reading, session min/max |
| Humidity | Live %RH reading, session min/max |
| LED brightness | Slider 0–100% with live watt estimation |
| Fan control | Independent intake and exhaust sliders |
| Light schedule | On/off time pickers, 24h visual timeline, enable toggle |
| System status | WiFi RSSI, uptime, PD status, free heap, grow day counter |
| Power summary | Estimated total wall draw |

---

## Camera Feed

The ESP32-CAM module mounts on the interior cabinet wall via a 3D printed bracket, angled downward to cover the full plant canopy. It connects to the hub board via a 4-wire UART cable.

<p align="center">
  <img src="docs/images/camera_mount.jpg" alt="ESP32-CAM mounted inside cabinet" width="500"/>
</p>

<p align="center">
  <img src="docs/images/camera_feed_plants.jpg" alt="Camera feed showing plants" width="600"/>
</p>

**Camera pipeline:**

```
OV2640 captures QVGA (320×240) JPEG
        │
        │ UART1 @ 921600 baud
        │ Framing: 0xFF 0xD8 [4-byte length] [JPEG] 0xFF 0xD9
        ▼
ESP32-S3 receives frame
        │
        │ HTTP multipart/x-mixed-replace
        ▼
Browser displays ~3fps MJPEG stream
```

On demand only — camera streams when a browser client has `/stream` open. ESP32-S3 sends `{"cmd":"stream","state":0}` when the client disconnects, stopping transmission and saving power.

---

## Wiring Overview

<p align="center">
  <img src="docs/images/wiring_diagram.jpg" alt="Full wiring diagram" width="800"/>
</p>

**Cable types used:**

| Connection | Connector | Wire | Current |
|-----------|-----------|------|---------|
| Hub → each LED driver board (power) | JST VH 2-pin | 20AWG | ~1A per board |
| Hub → each LED driver board (signal) | JST XH 2-pin | 26AWG UL2468 | <10mA |
| Driver board → LED strings | JST XH 2-pin | 26AWG UL2468 | 340mA |
| Hub → sensor board | JST XH 4-pin | 26AWG UL2468 | <5mA |
| Hub → fans | JST XH 2-pin | 24AWG | <500mA |
| Hub → ESP32-CAM | JST XH 4-pin | 26AWG | <300mA |

**Star topology for 20V power:**

```
Hub board
  ├──► LED Driver Board 1  (individual VH cable)
  ├──► LED Driver Board 2  (individual VH cable)
  ├──► LED Driver Board 3  (individual VH cable)
  └──► LED Driver Board 4  (individual VH cable)
```

Do not daisy-chain 20V through the driver boards. Each board gets its own direct cable from the hub to avoid cumulative voltage drop and overcurrent on a single connector.

---

## Features

- **USB-C PD negotiation** — full FUSB302 driver written from scratch in C. Requests 20V fixed PDO, handles Source_Capabilities, Accept, PS_RDY sequence. Retries every 30 seconds on failure. Detects cable disconnect and renegotiates automatically
- **LED brightness control** — 0–100% PWM via LEDC peripheral at 25kHz. Smooth 5-minute sunrise/sunset ramp on schedule transitions. Settings persisted to NVS
- **Light schedule** — configurable on/off time stored in NVS. SNTP time sync on WiFi connect. Timezone Europe/Brussels. Grow day counter from first boot timestamp
- **Fan control** — independent intake and exhaust speed 0–100%. Negative pressure maintained by running exhaust consistently higher than intake
- **Environment monitoring** — AHT20 polled every 10 seconds. Session min/max tracking for both temperature and humidity
- **Camera integration** — ESP32-CAM sends IP over UART on boot. Hub proxies MJPEG stream to browser on demand. Start/stop commands sent to camera over UART to save power when not viewing
- **Web dashboard** — fully self-contained single HTML file, dark theme, green accents, mobile responsive, no internet dependency
- **WiFi provisioning** — SoftAP captive portal on first boot for entering WiFi credentials. Saves to NVS, restarts, connects automatically on all subsequent boots
- **mDNS** — accessible at `growbox.local` after WiFi connect, no IP address needed

---

## API Endpoints

| Method | Endpoint | Description |
|--------|---------|-------------|
| GET | `/` | Web dashboard HTML |
| GET | `/api/status` | Full system status JSON |
| GET | `/api/sensors` | Temperature and humidity only |
| POST | `/api/led` | Set brightness `{"brightness": 0-100}` |
| POST | `/api/fans` | Set fan speeds `{"fan1": 0-100, "fan2": 0-100}` |
| GET | `/api/schedule` | Get light schedule |
| POST | `/api/schedule` | Set schedule `{"on":"06:00","off":"22:00","enabled":true}` |
| GET | `/api/camera` | Camera IP and stream URL |
| GET | `/stream` | MJPEG stream proxy from ESP32-CAM |
| POST | `/api/config/wifi` | Update WiFi credentials, triggers restart |

**Example `/api/status` response:**

```json
{
  "temp": 24.5,
  "humidity": 58.2,
  "temp_min": 22.1,
  "temp_max": 26.8,
  "humidity_min": 55.0,
  "humidity_max": 62.0,
  "led_brightness": 75,
  "led_watts": 48.75,
  "fan1_speed": 60,
  "fan2_speed": 75,
  "pd_negotiated": true,
  "pd_voltage_mv": 20000,
  "pd_current_ma": 3250,
  "wifi_rssi": -62,
  "uptime_seconds": 86423,
  "grow_day": 14,
  "lights_on": true,
  "minutes_until_transition": 342,
  "cam_ip": "192.168.1.105",
  "cam_online": true,
  "heap_free": 187432,
  "firmware_version": "1.0.0"
}
```

---

## Project Structure

```
growbox-firmware/
├── docs/
│   └── images/                     # All images referenced in this README
├── main/
│   ├── main.c                      # app_main, init sequence, task spawning
│   ├── fusb302.c / fusb302.h       # FUSB302 I2C register driver
│   ├── fusb302_pd.c / fusb302_pd.h # USB PD protocol state machine
│   ├── aht20.c / aht20.h           # AHT20 temperature/humidity driver
│   ├── led_control.c / led_control.h # LEDC LED and fan PWM control
│   ├── schedule.c / schedule.h     # Light schedule logic and SNTP
│   ├── wifi_manager.c / wifi_manager.h # WiFi, mDNS, captive portal
│   ├── http_server.c / http_server.h   # All HTTP endpoint handlers
│   ├── uart_cam.c / uart_cam.h     # ESP32-CAM UART protocol
│   ├── nvs_config.c / nvs_config.h # NVS read/write helpers
│   ├── state.c / state.h           # Global state struct and mutex
│   ├── dashboard.h                 # Embedded dashboard HTML string
│   └── CMakeLists.txt
├── partitions.csv                  # Custom partition table
├── sdkconfig.defaults              # Default SDK configuration
├── CMakeLists.txt
└── README.md
```

---

## FreeRTOS Task Structure

| Task | Stack (bytes) | Priority | Core |
|------|-------------|----------|------|
| pd_task | 4096 | 5 | 0 |
| wifi_task | 4096 | 4 | 0 |
| uart_cam_task | 4096 | 4 | 1 |
| sensor_task | 2048 | 3 | 0 |
| schedule_task | 2048 | 2 | 1 |

HTTP server runs in its own internal task via `esp_http_server`.

Inter-task communication:
- `pd_event_queue` — depth 8, FUSB302 interrupt events from GPIO ISR to pd_task
- `sensor_data_queue` — depth 2, latest AHT20 readings

All shared state lives in a single global struct protected by one FreeRTOS mutex. HTTP handlers read from this struct under the mutex. No blocking calls in HTTP handler context.

---

## NVS Configuration

Namespace: `growbox`

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `wifi_ssid` | string | — | WiFi network name |
| `wifi_pass` | string | — | WiFi password |
| `led_brightness` | uint8 | 70 | LED brightness percent |
| `fan1_speed` | uint8 | 50 | Intake fan speed percent |
| `fan2_speed` | uint8 | 60 | Exhaust fan speed percent |
| `sched_on` | uint16 | 360 | Light on time (minutes since midnight, 360 = 06:00) |
| `sched_off` | uint16 | 1320 | Light off time (minutes since midnight, 1320 = 22:00) |
| `sched_enabled` | uint8 | 1 | Schedule active flag |
| `first_boot_ts` | uint32 | 0 | Unix timestamp of first boot for grow day counter |
| `firmware_ver` | string | "1.0.0" | Firmware version string |

---

## Building and Flashing

### Prerequisites

- ESP-IDF v5.x installed and sourced (`source $IDF_PATH/export.sh`)
- USB-C cable connected to hub board (USB Serial/JTAG on GPIO19/GPIO20, no separate programmer needed)

### Clone and build

```bash
git clone https://github.com/yourusername/growbox-firmware
cd growbox-firmware
idf.py set-target esp32s3
idf.py build
```

### Flash

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

On first boot with no WiFi credentials in NVS, the device starts a setup access point:

```
SSID:     GrowBox-Setup
Password: growbox123
URL:      http://192.168.4.1
```

Connect to this network, open the URL, enter your WiFi credentials. Device restarts and connects. Access the dashboard at:

```
http://growbox.local
```

### JTAG debug

USB Serial/JTAG is available natively on GPIO19/GPIO20 via the USB-C connector. No external probe needed. Use OpenOCD with the ESP32-S3 built-in target:

```bash
openocd -f board/esp32s3-builtin.cfg
>>>>>>> 2fca156416b63ba39733e7f331657d6a7e1e1554
```

---

<<<<<<< HEAD
## First boot / WiFi setup

With no WiFi credentials stored, the device starts a SoftAP
**`GrowBox-Setup`** (password `growbox123`). Connect to it and browse to
`192.168.4.1` to enter your real WiFi credentials; the device saves them
to NVS and restarts. On subsequent boots it connects directly and the
dashboard becomes reachable at `http://growbox.local/` (mDNS) or its
DHCP-assigned IP.

## Passcode

The dashboard's lock screen passcode is `4712` by default, stored in NVS
key `auth_code` under the `growbox` namespace. There's currently no HTTP
endpoint to change it — only to log in with it.

## Known gaps / things to bench-test

- **USB-PD negotiation** (`fusb302.c`, `fusb302_pd.c`) is a from-scratch
  driver against the FUSB302B register map and USB PD 2.0 message
  format. It hasn't been run against real silicon or a real charger.
  Budget time with a logic analyzer on the CC line — PD signaling is
  unforgiving of a single wrong register bit, and reference drivers
  vary in the exact FIFO token sequence they use.
- **No SD logging / no fan auto-mode**: both removed per request. The
  dashboard's fan sliders are pure manual control, and there's no
  historical data logging anywhere (`/api/log` doesn't exist).
- **Auth** (`auth.c`, `POST /api/auth`) isn't in the original firmware
  spec — it's there because the dashboard already implements a passcode
  + bearer-token flow and needed something to talk to. Fine for a
  LAN-only home device; not hardened against an attacker who already
  has network access to it.
- **Dev container USB access** uses `--privileged` + a full `/dev` bind
  mount (see `.devcontainer/devcontainer.json`) so the board's serial
  port is visible without hardcoding its exact path. That's broad device
  access — a reasonable tradeoff for a personal local dev container, but
  worth knowing it's not a sandboxed container in the usual sense.
=======
## ESP32-CAM Setup

The ESP32-CAM runs independently on the same WiFi network. Flash it with ESP32-CAM-WebServer firmware modified to:

1. Connect to the same WiFi network as the hub
2. Send its IP address over UART at 921600 baud on boot: `{"cam_ip":"192.168.1.x"}\n`
3. Accept stream start/stop commands over UART: `{"cmd":"stream","state":1}`
4. Output QVGA (320×240) JPEG frames framed as: `0xFF 0xD8` [4-byte big-endian length] [JPEG data] `0xFF 0xD9`

The hub stores the camera IP in RAM on receipt and uses it to construct the stream proxy URL. Assign the ESP32-CAM a static IP in your router DHCP settings for reliability.

**Physical mounting:**

Mount the ESP32-CAM on the interior cabinet wall at approximately 2/3 height, angled 30–45° downward toward the canopy center. A simple 3D printed bracket with a snap-fit camera slot works well. Run the 4-wire UART cable along the cabinet wall to the hub board.

---

## Grow Schedule Recommendations

| Crop | Light on | Light off | Hours | Brightness |
|------|---------|----------|-------|-----------|
| Seedlings (all) | 06:00 | 22:00 | 16h | 40% |
| Vegetative cannabis | 06:00 | 00:00 | 18h | 70% |
| Autoflower full cycle | 06:00 | 00:00 | 18h | 75% |
| Photoperiod flowering | 08:00 | 20:00 | 12h | 85% |
| Peppers / chili | 06:00 | 22:00 | 16h | 65% |
| Herbs / lettuce | 06:00 | 22:00 | 16h | 55% |
| Microgreens | 06:00 | 22:00 | 16h | 45% |

**Estimated power at different brightness levels:**

| Brightness | LED power | Total estimated wall draw |
|-----------|----------|--------------------------|
| 40% | 26W | ~30W |
| 50% | 32.5W | ~37W |
| 65% | 42.3W | ~47W |
| 75% | 48.8W | ~54W |
| 85% | 55.3W | ~61W |
| 100% | 65W | ~72W |

---

## Bill of Materials

### Hub Board

| Component | Part | LCSC | Qty | Unit | Total |
|-----------|------|------|-----|------|-------|
| ESP32-S3 SoC | ESP32-S3 QFN-56 | — | 1 | ~€2.50 | €2.50 |
| NOR Flash | GD25WQ64ESIGR | — | 1 | ~€0.80 | €0.80 |
| PD controller | FUSB302UCX | C481901 | 1 | €0.46 | €0.46 |
| 3.3V buck | SY8120B | — | 1 | €0.15 | €0.15 |
| Crystal 40MHz | ±10ppm | — | 1 | €0.20 | €0.20 |
| USB-C receptacle | 16P SMD | C165948 | 1 | €0.18 | €0.18 |
| AHT20 (on sensor board) | AHT20 | C2757724 | 1 | €0.58 | €0.58 |
| Passives + connectors | — | — | — | — | ~€2.00 |
| **Hub total** | | | | | **~€6.87** |

### LED Driver Boards (×4)

| Component | Part | LCSC | Qty/board | Unit | Per board | ×4 |
|-----------|------|------|----------|------|----------|-----|
| TX6120 driver | TX6120 ESOP-8 | C329272 | 3 | €0.26 | €0.78 | €3.12 |
| Inductor 330µH | Isat ≥500mA shielded | — | 3 | €0.25 | €0.75 | €3.00 |
| Schottky SS14 | 1A 40V SOD-123 | C2480 | 3 | €0.03 | €0.09 | €0.36 |
| R_sense 0.75Ω | 1% 0603 | — | 3 | €0.02 | €0.06 | €0.24 |
| R_VDD 7.2kΩ | 1% 0805 | — | 3 | €0.01 | €0.03 | €0.12 |
| C_in 10µF 25V | X7R 0805 | — | 3 | €0.03 | €0.09 | €0.36 |
| Passives + connectors | — | — | — | — | €0.30 | €1.20 |
| **Per board** | | | | | **€2.10** | **€8.40** |

### LED Panel Boards (×4)

| Component | Part | LCSC | Qty/board | Unit | Per board | ×4 |
|-----------|------|------|----------|------|----------|-----|
| Warm white LED | XL-3030WWC-1W-3V | C2843893 | 15 | €0.034 | €0.51 | €2.04 |
| Connectors + passives | — | — | — | — | €0.20 | €0.80 |
| **Per board** | | | | | **€0.71** | **€2.84** |

### Other

| Item | Cost |
|------|------|
| ESP32-CAM module | ~€3.00 |
| 100W GaN USB-C charger | ~€20.00 |
| 120mm PC fans ×2 | ~€6.00 |
| JST cables and connectors | ~€3.00 |
| Aquarium activated carbon | ~€4.00 |
| MDF + hardware for cabinet | ~€25.00 |
| Mylar reflective film | ~€5.00 |
| Aluminum backing plates ×4 | ~€4.00 |
| EPDM door seal + latches | ~€5.00 |

**Total estimated build cost: ~€95–110**

---

## License

MIT License — see [LICENSE](LICENSE) file.

---

<p align="center">
  Made by Arik · IoT & Electronics Engineering Student · Belgium
</p>

<p align="center">
  <img src="docs/images/growbox_plants.jpg" alt="Plants growing inside the cabinet" width="600"/>
</p>
>>>>>>> 2fca156416b63ba39733e7f331657d6a7e1e1554
