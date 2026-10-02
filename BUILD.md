# Build & Technical Reference

Part of the [GrowBox](README.md) build. This is the engineering reference —
GPIO pinout, wiring, connectors, NVS config, task structure, and build/flash
steps. For photos and an overview of the build, see the [README](README.md).
Parts list and cost: [BOM.md](BOM.md).

---

## System Architecture

```
                        USB-C PD Charger (100W GaN)
                                  │
                                  │ 20V @ up to 5A
                                  ▼
                    ┌──────────────────────────────┐
                    │        MAIN HUB BOARD        │
                    │                              │
                    │  FUSB302 ──► 20V rail        │
                    │  ESP32-S3                    │
                    │  AP63203 Buck ──► 3.3V logic │
                    │  AHT20 sensor board (ext.)   │
                    │  ESP32-CAM (ext. via UART)   │
                    └──────────┬───────────────────┘
                               │
               ┌───────────────┼───────────────┐
               │               │               │
               ▼               ▼               ▼
        LED Board 1      LED Board 2     LED Board 3+4
       (4× TX6120)      (4× TX6120)     (4× TX6120 each)
       20 LEDs           20 LEDs         20 LEDs each
       ~21.8W            ~21.8W          ~21.8W each
```

All four LED driver boards receive:
- **20V power** via individual JST VH 2-pin cables (star topology from hub)
- **DIM_PWM signal** via 2-pin signal cable (daisy-chainable, milliamp level)

---

## PCB Details

All four boards designed in KiCad, manufactured at JLCPCB.

### 1. Main Hub Board

**Key components:**

| Component | Part | Description |
|-----------|------|-------------|
| MCU | ESP32-S3 QFN-56 | Dual-core 240MHz, WiFi+BLE, USB JTAG |
| Flash | GD25WQ64ESIGR | 8MB NOR SPI flash, WSON-8 |
| PD controller | FUSB302UCX | USB-C PD sink, I2C controlled |
| 3.3V regulator | AP63203 | 32V input, 2A buck |
| USB-C receptacle | 16-pin SMD | Power + CC1/CC2 for FUSB302 + D+/D- for JTAG |
| Crystal | 40MHz ±10ppm | Required for bare SoC |

**Connectors:**

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
| GPIO4 | SPI MOSI (SD card) |
| GPIO5 | SPI MISO (SD card) |
| GPIO6 | SPI CLK (SD card) |
| GPIO8 | I2C SDA |
| GPIO9 | I2C SCL |
| GPIO10 | FUSB302 INT# |
| GPIO11 | LED DIM PWM |
| GPIO12 | Fan 1 PWM |
| GPIO13 | Fan 2 PWM |
| GPIO17 | UART1 RX (camera) |
| GPIO18 | UART1 TX (camera) |
| GPIO19 | USB D- (JTAG) |
| GPIO20 | USB D+ (JTAG) |
| GPIO41 | SD card CS |

### 2. LED Driver Board

One of four identical boards, 4× TX6120 buck constant-current drivers each.

**Per-channel design (×4 per board):**

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
Power per board  = 4 strings × 5.44W = 21.76W
Total (4 boards) = 4 × 21.76W = 87W at 100% PWM
```

**Connectors:**

| Connector | Type | Signal |
|-----------|------|--------|
| J1 | JST VH 2-pin | 20V power in from hub |
| J2 | JST XH 2-pin | DIM_PWM + GND from hub |
| J3–J6 | JST XH 2-pin | LED+ and LED- per string (4 strings out) |

### 3. LED Panel Board

One of four identical boards, 20× XL-3030WWC-1W-3V warm white LEDs each (80
total). Single-layer FR4 board with an aluminum bottom layer — not a
separate bolted-on plate.

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
| Count per board | 20 (4 strings × 5 series) |

**Thermal path:** LED thermal pad → 9× vias (0.3mm drill) per LED → B.Cu
copper pour (full board flood) → thermal paste (Shin-Etsu X-23 or
equivalent) → aluminum bottom layer, integral to the board.

**Board dimensions:** 85 × 55mm, FR4 1.6mm, HASL finish

**Connectors:**

| Connector | Type | Signal |
|-----------|------|--------|
| J1–J4 | JST XH 2-pin | LED+ and LED- per string (4 strings in from driver board) |

### 4. Sensor Board

Minimal standalone board, AHT20 only. Connects via 4-pin I2C cable; mount
away from heat sources for accurate ambient readings.

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

Pull-ups live on the sensor board rather than the hub so it's self-contained
and plug-and-play on any I2C bus.

---

## Wiring

**Cable types:**

| Connection | Connector | Wire | Current |
|-----------|-----------|------|---------|
| Hub → each LED driver board (power) | JST VH 2-pin | 20AWG | ~1A per board |
| Hub → each LED driver board (signal) | JST XH 2-pin | 26AWG UL2468 | <10mA |
| Driver board → LED strings | JST XH 2-pin | 26AWG UL2468 | 340mA |
| Hub → sensor board | JST XH 4-pin | 26AWG UL2468 | <5mA |
| Hub → fans | JST XH 2-pin | 24AWG | <500mA |
| Hub → ESP32-CAM | JST XH 4-pin | 26AWG | <300mA |

**Star topology for 20V power** — each driver board gets its own direct
cable from the hub; don't daisy-chain 20V through the boards, to avoid
cumulative voltage drop and overcurrent on a single connector:

```
Hub board
  ├──► LED Driver Board 1  (individual VH cable)
  ├──► LED Driver Board 2  (individual VH cable)
  ├──► LED Driver Board 3  (individual VH cable)
  └──► LED Driver Board 4  (individual VH cable)
```

---

## Camera pipeline

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

On demand only — camera streams while a browser client has `/stream` open.
ESP32-S3 sends `{"cmd":"stream","state":0}` on client disconnect, stopping
transmission and saving power.

### ESP32-CAM setup

Has no network connectivity of its own — see [esp32cam/README.md](esp32cam/README.md)
for the actual firmware (bare ESP-IDF, not ESP32-CAM-WebServer). Everything
goes over the dedicated UART link to the hub:

1. Announces it's up over UART at 921600 baud on boot: `{"ready":true}\n`
2. Accepts stream start/stop commands over UART: `{"cmd":"stream","state":1}`
3. Outputs QVGA (320×240) JPEG frames framed as: `0xFF 0xD8` [4-byte big-endian length] [JPEG data] `0xFF 0xD9`

The hub tracks camera liveness in RAM from the `ready` message and from
frames actually arriving — there's no IP address anywhere in this picture,
since the video never leaves the UART link.

**Mounting:** interior cabinet wall, ~2/3 height, angled 30–45° downward
toward canopy center. 3D printed bracket with a snap-fit slot; run the
4-wire UART cable along the wall to the hub board.

---

## Project structure

```
microgrowery/
├── README.md                    # showcase
├── BUILD.md                     # this file
├── BOM.md                       # parts list and cost
├── dashboard.html               # browser dashboard (source of truth)
├── Dockerfile                   # ESP-IDF build image
├── docker-compose.yml           # build/flash/monitor services
├── .devcontainer/                # VS Code dev container config
├── scripts/                     # host-side build/flash/monitor helpers
├── docs/images/                 # all images referenced in the README
└── firmware/                    # ESP-IDF project
    ├── CMakeLists.txt
    ├── partitions.csv
    ├── sdkconfig.defaults
    └── main/
        ├── main.c                        # app_main, init sequence, task spawning
        ├── fusb302.c / fusb302.h         # FUSB302 I2C register driver
        ├── fusb302_pd.c / fusb302_pd.h   # USB PD protocol state machine
        ├── aht20.c / aht20.h             # AHT20 temperature/humidity driver
        ├── led_control.c / led_control.h # LEDC LED and fan PWM control
        ├── schedule.c / schedule.h       # Light schedule logic and SNTP
        ├── wifi_manager.c / wifi_manager.h # WiFi, mDNS, captive portal
        ├── auth.c / auth.h               # Dashboard passcode + bearer tokens
        ├── http_server.c / http_server.h # All HTTP endpoint handlers
        ├── uart_cam.c / uart_cam.h       # ESP32-CAM UART protocol
        ├── nvs_config.c / nvs_config.h   # NVS read/write helpers
        ├── state.c / state.h             # Global state struct and mutex
        ├── dashboard.html                # embedded copy of the root dashboard.html
        └── CMakeLists.txt
```

---

## FreeRTOS task structure

| Task | Stack (bytes) | Priority | Core |
|------|-------------|----------|------|
| pd_task | 4096 | 5 | 0 |
| wifi_task | 4096 | 4 | 0 |
| uart_cam_task | 4096 | 4 | 1 |
| sensor_task | 2048 | 3 | 0 |

HTTP server runs in its own internal task via `esp_http_server`. The light
schedule isn't a dedicated task — it's a periodic `esp_timer` callback,
checked every 60 seconds.

Inter-task communication: `pd_event_queue` (depth 8) carries FUSB302
interrupt events from the GPIO ISR to `pd_task`. Everything else — sensor
readings, LED/fan/schedule/PD/WiFi status — lives in one global struct
protected by a single FreeRTOS mutex; `sensor_task`, `pd_task` and the HTTP
handlers all read/write it under that lock rather than passing data through
queues.

---

## NVS configuration

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
| `first_boot_ts` | uint32 | 0 | Unix timestamp of first boot, for grow day counter |
| `auth_code` | string | "4712" | Dashboard lock-screen passcode |

---

## Building and flashing

Firmware lives in `firmware/`, built via Docker — no local ESP-IDF install
needed. Open the repo root in VS Code and accept "Reopen in Container" to
build from the integrated terminal (`idf.py build`), or run manually from
the repo root:

```bash
docker compose run --rm build
docker compose run --rm flash     # needs USB passthrough — see docker-compose.yml's header comment
docker compose run --rm monitor
```

First build needs internet access once — `main/idf_component.yml` pulls
in Espressif's `mdns` component from the registry (it moved out of core
ESP-IDF as of the v5.2.x line, so it's no longer bundled).

The dev container itself has no device access on purpose (a hard-coded
device mount breaks container creation on Windows, where there's no real
`/dev` for Docker Desktop to bind from) — flash and monitor from the host,
or from a plain terminal using the `docker compose` commands above with
USB passthrough set up.

Or flash from the host directly (`pip install esptool`), which sidesteps
USB passthrough entirely:

```bash
scripts/flash.sh /dev/ttyUSB0     # or COM5 on Windows
scripts/monitor.sh /dev/ttyUSB0
```

**Hardware note:** USB Serial/JTAG is native on GPIO19/GPIO20 via the
USB-C connector — no external programmer or probe needed, for flashing or
for OpenOCD debugging (`openocd -f board/esp32s3-builtin.cfg`).

### First boot / WiFi setup

With no WiFi credentials stored, the device starts a SoftAP:

```
SSID:     GrowBox-Setup
Password: growbox123
URL:      http://192.168.4.1
```

Connect, open the URL, enter your WiFi credentials. Device restarts,
connects, and the dashboard becomes reachable at `http://growbox.local`.

---

## Power budget

Estimated power at different LED brightness levels:

| Brightness | LED power | Total estimated wall draw |
|-----------|----------|--------------------------|
| 40% | 34.8W | ~40W |
| 50% | 43.5W | ~49W |
| 65% | 56.6W | ~62W |
| 75% | 65.3W | ~70W |
| 85% | 74.0W | ~79W |
| 100% | 87W | ~92W |

## Thermal analysis — LED board cooling

Each LED board dissipates 21.76W at 100% brightness (20× XL-3030WWC-1W-3V,
1.088W each — see [LED Panel Board](#3-led-panel-board)) through an 85×55mm
single-layer FR4 board with an aluminum bottom layer, no separate bolted-on
plate. That's roughly 4.6W/cm² of board area, which is a lot of heat flux
for passive cooling at that footprint — hence the heatsink + fan retrofit
(2× 20×20×80mm extruded aluminum heatsinks per board, each pasted to the
aluminum backing, fed by a dedicated 3600rpm 5020 blower through a ~200mm
duct run in series across both heatsinks — see photo above).

**Disclaimer:** no published thermal-resistance datasheet exists for this
exact XINGLIGHT LED SKU, for a generic 20×20×80mm heatsink, or for this
specific blower/duct combination. The figures below are an engineering
estimate built from the board's real electrical data (this doc) plus
typical thermal-resistance ranges for parts in this class — not a lab
measurement. Validate with a thermocouple or IR thermometer on the real
board before trusting these as hard limits.

**Model:**
```
T_junction = T_ambient + P_board × R_board-to-ambient + (P_board / 20) × R_led(j-to-solderpoint)
```
- R_led(j-sp) ≈ 8–15°C/W (typical for a 1W SMD-3030 package), 12°C/W used as the mid estimate
- T_ambient assumed 30°C cabinet interior — rescale linearly for your measured ambient
- Target ceilings: 125°C = typical absolute Tj max for this LED class (hard limit, not for sustained use); 105°C = recommended sustained limit for reasonable lumen-maintenance/lifetime

| Cooling stage | R_board-ambient (est.) | Max continuous W/board @105°C | Max continuous W/board @125°C | As brightness % |
|---|---|---|---|---|
| Bare board, no heatsink | ~18°C/W (14–24) | ~4W (3–5W) | ~5W (4–7W) | ~18–23% |
| + 2× heatsink, no fan (natural convection) | ~6°C/W (4–9) | ~11W (8–17W) | ~14W (10–22W) | ~50–66% |
| + heatsink + 3600rpm blower + duct (forced air) | ~2°C/W (1.2–3) | full 21.76W, Tj ≈ 87°C | full 21.76W, Tj ≈ 87°C | 100% |

The bare-board figure confirms the heatsinks weren't optional — without
them the board is thermally limited to roughly a fifth of rated power.
Heatsinks alone roughly triple the safe ceiling but likely still can't
sustain 100% brightness on natural convection. With the fan, heat stops
being the limiting factor at all — the driver's 340mA current-sense
ceiling becomes the real limit instead.

**One geometry detail that matters:** the duct is a single pass feeding
both heatsinks in series, not a Y-split to each individually, so the
downstream heatsink gets air that's already picked up heat from the
upstream one. Accounting for that asymmetry (~half the board's heat,
10.9W, through each heatsink zone; downstream assumed 30–40% less
effective due to pre-warmed inlet air):

| Heatsink position | Est. junction temp at 100% brightness |
|---|---|
| Upstream (fresh air from blower) | ~80°C |
| Downstream (pre-warmed air from first heatsink) | ~95°C |

Worst case (the downstream end of the duct) still lands under the 105°C
recommended ceiling, with roughly 10°C of margin — thinner than a naive
symmetric-heatsink estimate would suggest, and worth spot-checking in
person at that specific spot rather than assuming it from the model. If
more margin is ever needed, splitting the duct so both heatsinks draw
fresh air in parallel instead of in series would even out — and lower —
the hottest point.
