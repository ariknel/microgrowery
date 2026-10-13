# GrowBox Fan Controller

CH32F4P6 (WCH, ARM Cortex-M4F) firmware on a custom PCB, independent of
the ESP32-S3 hub and ESP32-CAM — see
[../BUILD.md#thermal-analysis--led-board-cooling](../BUILD.md#thermal-analysis--led-board-cooling)
for why the LED heatsinks need active cooling in the first place, and
[../README.md](../README.md) for the cabinet-level overview.

This replaced an earlier Arduino Nano perfboard prototype (same control
logic, different hardware) — the prototype photo is still in the main
README for reference, but the board described here is what's actually
being built.

## What it does

4 independent zones, one per LED board. Each zone has its own NTC
thermistor — mounted directly on that board's PCB, not on a heatsink, so
it reads that specific board's temperature rather than general cabinet
airflow — and its own MOSFET switching that board's 2 fans together. A
board running hotter than the other 3 only speeds up its own 2 fans, not
all 8; each board's cooling is independent of the others.

Runs standalone on purpose: fan cooling keeps working even if the hub
reboots, the camera hangs, or WiFi is down — this board shares no pins or
power with either (it's powered off the hub's 12V rail through its own
onboard AMS1117-3.3, not from the hub's logic supply).

**Not implemented in this port yet:** the Nano prototype reported temps
and fan duty to the hub over UART (see git history / BUILD.md). That
needed a spare TX pin, and the pinout below doesn't have one allocated —
rather than guess a pin on an already-laid-out PCB, that feature is just
left out for now. Cooling doesn't depend on it either way; it was always
read-only visibility, never a control input.

## Hardware

| Component | Part |
|---|---|
| MCU | CH32F4P6 (WCH, ARM Cortex-M4F) |
| Fan switches | 4× Si2302 N-channel MOSFET (SOT-23), one per LED board's pair of fans |
| NTC | HNTC-104F3950FB, 100kΩ @25°C, B=3950, ±1% ([LCSC C52204613](https://lcsc.com/product-detail/C52204613.html)) |
| Power | 12V hub rail → onboard AMS1117-3.3 → CH32F4P6 + MOSFET gate drive |

Running the MCU at 3.3V (not the Nano's 5V) is what let the hub-telemetry
voltage divider from the prototype go away — see "Changes from the Nano
prototype" below. Si2302 is logic-level enough at 3.3V Vgs for this load
(a couple hundred mA per MOSFET, 2 small blowers each) — no gate driver
needed, same as the Nano design's logic-level FQP30N06L.

**Pinout:**

| Net | CH32F4P6 pin | Function |
|---|---|---|
| PWM_1 | PD2 (pin 19) | TIM1_CH1 |
| PWM_2 | PA1 (pin 5) | TIM1_CH2 |
| PWM_3 | PC3 (pin 13) | TIM1_CH3 |
| PWM_4 | PC4 (pin 14) | TIM1_CH4 |
| NTC_1 | PA2 (pin 6) | ADC, channel 0 |
| NTC_2 | PD3 (pin 20) | ADC, channel 4 |
| NTC_3 | PD4 (pin 1) | ADC, channel 7 |
| NTC_4 | PD6 (pin 3) | ADC, channel 6 |
| SWIO | PD1 (pin 18) | Programming/debug — not touched by application code |

Zone index 0–3 in the firmware = PWM_1/NTC_1 .. PWM_4/NTC_4 = LED board
1–4. Keep that mapping consistent with however the boards are physically
labeled in the cabinet.

NTC divider (unchanged from the prototype — see "Changes from the Nano
prototype"):

```
3.3V ---[NTC: 100k]---+---[47k]--- GND
                       |
                   ADC input
```

## Fan curve (tune these against a real thermocouple)

| Board temp | That board's 2 fans |
|---|---|
| < 35°C | off |
| 35°C – 55°C | ramps 35%–100% |
| ≥ 55°C | full speed |
| that zone's sensor open/shorted | that zone fails to full speed; other 3 zones unaffected |

Thresholds are `#define`s at the top of `User/main.c` — the 35°C/55°C
numbers are starting points, not measured values; see the thermal
analysis in BUILD.md for why (no datasheet thermal-resistance data exists
for these exact parts, so nothing in that chain is lab measured yet).

## Noise

Each 5015 blower is rated ~24-28dB. Sound pressure from N identical
incoherent sources adds `10*log10(N)` dB, so all 8 fans (4 boards × 2
each) at full speed simultaneously is roughly a 9dB bump over one fan
alone — about **33-37dB combined**, treating all 8 as equidistant (a
rough worst-case estimate, not a measurement). Because the 4 boards share
the same dashboard brightness setting, they'll usually run close to the
same temperature and ramp up together in practice, even though each
zone's speed is computed independently — the "independent zones" design
is mainly about catching the board that's hotter than the others (worse
airflow, a bad thermal pad, etc.), not about normal operation being
wildly uneven. At low brightness all 4 zones stay off or quiet.

## Build

Needs WCH's EVT peripheral library, which **isn't vendored in this repo**
— WCH distributes it as a zip from their own site, not a package
registry a Dockerfile could pull from automatically. Download the CH32F4
EVT package from WCH's site and copy into `Libraries/`:

```
fan_controller/Libraries/
├── CMSIS/              (Core/ + Device/ headers)
├── Peripheral/
│   ├── inc/             (ch32f4xx_rcc.h, _gpio.h, _tim.h, _adc.h, etc.)
│   └── src/
├── Startup/
│   └── startup_ch32f40x.S
└── Link.ld
```

Paths above match WCH's typical EVT layout as of this writing — if your
download extracts differently, adjust the path variables at the top of
`Makefile` rather than moving files around to match.

```bash
docker compose run --rm build   # make
docker compose run --rm clean   # make clean
docker compose run --rm shell   # drop into a shell with the toolchain on PATH
```

This builds `build/fan_controller.elf/.hex/.bin`. Flashing itself isn't
containerized — use WCH-LinkE's own host-side tools (WCH-LinkUtility or
`MRS_Programmer`/`wlink`, depending on what WCH currently ships) pointed
at the `.hex`, connected via the `SWIO` pin above.

**Honesty note on the firmware itself:** `User/main.c` was written
without WCH's actual EVT header files in hand — it targets WCH's
Standard-Peripheral-Library-style API (CH32F4 is marketed as
register/pin-compatible with STM32F405, and WCH's library mirrors STM32
SPL conventions), but exact function/macro names (especially the GPIO
alternate-function config for routing PD2/PA1/PC3/PC4 to TIM1, and the
`ADC_Channel_*` constants) need a real compile check against your
vendored `Libraries/` before trusting them. Flagged inline in the source
wherever a call is a best-effort match rather than verified API.

## Changes from the Nano prototype

- **MCU:** Arduino Nano (ATmega328P, 5V logic) → CH32F4P6 (ARM
  Cortex-M4F, 3.3V logic, powered via onboard AMS1117-3.3 off the 12V
  rail instead of USB).
- **MOSFETs:** FQP30N06L (TO-220, through-hole) → Si2302 (SOT-23, custom
  PCB) — same logic-level direct-GPIO-drive approach, much smaller
  package for a load this light.
- **NTC divider:** unchanged — still 100k NTC / 47k series. The divider
  is ratiometric (supply and ADC reference are the same rail), so the
  5V→3.3V change didn't require a different resistor. What *did* change
  is ADC resolution: the code now reads 12-bit (0–4095) instead of the
  Nano's 10-bit (0–1023).
- **Hub telemetry:** dropped in this port (see "What it does" above) —
  the prototype's voltage-divider UART link to the hub's `J14` header no
  longer applies since there's no TX pin allocated on this board yet.
- **Toolchain:** Arduino IDE / `.ino` → plain C + `arm-none-eabi-gcc`,
  built through Docker like `../firmware` and `../esp32cam` (see "Build"
  above) — no vendor IDE needed, though WCH's EVT library still has to be
  supplied manually since it isn't on a public package registry the way
  `idf_component.yml` pulls ESP-IDF components.

## Known gaps

- `User/main.c`'s peripheral-library calls are unverified against real
  WCH headers — see the honesty note under "Build".
- Thresholds are unverified placeholders — needs real thermocouple
  validation on each board, same caveat as the rest of the thermal
  analysis this design is based on.
- No tachometer feedback (fans' 3rd wire, if present, isn't read) — the
  controller can't detect a stalled or disconnected fan on a given zone,
  only react to that zone's temperature.
- No hub telemetry in this port (see "What it does").
- `HardFault`/`BusFault`/etc. handlers (`User/ch32f4xx_it.c`) just halt
  rather than forcing fans to a safe state first — a watchdog-driven
  fail-safe would be more thorough but needs care to get right from fault
  context; not attempted here.
- An NTC mounted on the PCB (rather than on the heatsink itself) reads
  board temperature, which tracks junction temperature more directly but
  lags heatsink temperature slightly — fine for this fan curve's purpose,
  worth knowing if you ever compare readings against the heatsink-surface
  numbers in the BUILD.md thermal analysis.

Nano prototype schematic (superseded, kept for reference):
[docs/Schematics/fan_controller.pdf](../docs/Schematics/fan_controller.pdf).
