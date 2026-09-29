<p align="center">
  <img src="docs/images/growbox_hero.jpg" alt="GrowBox Cabinet" width="800"/>
</p>

<h1 align="center">🌱 GrowBox</h1>

<p align="center">
  Open-source USB-C powered smart microgrowery<br/>
  ESP32-S3 hub · 87W LED panel · WiFi dashboard · Camera streaming · USB-C PD
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
- [PCB Boards](#pcb-boards)
- [Web Dashboard](#web-dashboard)
- [Camera Feed](#camera-feed)
- [Features](#features)
- [Grow Schedule Recommendations](#grow-schedule-recommendations)
- [Build & Technical Reference](BUILD.md)
- [Bill of Materials](BOM.md)
- [License](#license)

---

## Overview

GrowBox is a fully custom USB-C powered microgrowery designed to fit under a desk. The controller drives an 87W warm white LED grow panel, controls intake and exhaust ventilation fans, monitors temperature and humidity, streams a live camera feed, and serves a local web dashboard — all from a single USB-C cable.

Everything is custom designed: four separate PCBs, ESP32-S3 firmware in bare C using ESP-IDF, and a self-contained HTML dashboard served directly from the microcontroller.

<p align="center">
  <img src="docs/images/growbox_open_door.jpg" alt="Cabinet open showing plants" width="700"/>
</p>

**Key specs at a glance:**

| Parameter | Value |
|-----------|-------|
| Input power | USB-C PD 20V via FUSB302 |
| Max LED power | 87W (80× warm white LEDs) |
| LED panel | 4× boards in a grid |
| Cabinet size | 600 × 450 × 720mm (external) |
| MCU | ESP32-S3 bare SoC, 8MB NOR flash |
| Connectivity | WiFi 802.11 b/g/n, mDNS `growbox.local` |
| Environment sensor | AHT20 temperature + humidity |
| Camera | ESP32-CAM MJPEG stream |
| Fan control | 2× PWM fans, independent speed |

---

## Cabinet

Constructed from 12mm MDF, interior walls painted white for light distribution. A DIY activated carbon filter with a 120mm exhaust fan handles odor control.

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

```
External:   600 × 450 × 720mm  (W × D × H)
Internal:   576 × 426 × 696mm  (external minus 2× 12mm MDF wall per axis)
Fan intake:  120mm, bottom-right panel
Fan exhaust: 120mm, top-right panel
Carbon filter: 3D printed housing, 80mm activated carbon bed, inline on exhaust
```

<p align="center">
  <img src="docs/images/kast-1.png" alt="Cabinet technical drawing" width="600"/>
</p>

---

## PCB Boards

Four custom PCBs, designed in KiCad and manufactured at JLCPCB. Full GPIO pinout, connector tables and component values live in [BUILD.md](BUILD.md).

### 1. Main Hub Board

The brain of the system — ESP32-S3, FUSB302 PD sink negotiation, AP63203 3.3V regulator, 8MB flash, and JST connectors for every other board and peripheral.

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

### 2. LED Driver Board

One of four identical boards — 4× TX6120 constant-current drivers each, powering 4 strings of 5 LEDs (~21.8W per board, ~87W across all four).

<p align="center">
  <img src="docs/images/pcb_driver_top.jpg" alt="Driver board top assembled" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_driver_bottom.jpg" alt="Driver board bottom" width="500"/>
</p>

<p align="center">
  <img src="docs/images/driver_front.PNG" alt="Driver board KiCad 3D render" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/driver_back.PNG" alt="Driver board KiCad 3D render back" width="500"/>
</p>

<p align="center">
  <img src="docs/images/pcb_driver_array.jpg" alt="All 4 driver boards side by side" width="700"/>
</p>

### 3. LED Panel Board

One of four identical boards — 20× XL-3030WWC-1W-3V warm white LEDs each (80 total), single-layer FR4 with an aluminum bottom layer for heat spreading.

<p align="center">
  <img src="docs/images/pcb_led_top_off.jpg" alt="LED board not powered" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/pcb_led_top_on.jpg" alt="LED board powered on" width="500"/>
</p>

<p align="center">
  <img src="docs/images/led_front.PNG" alt="LED board KiCad 3D render" width="500"/>
  &nbsp;&nbsp;
  <img src="docs/images/led_back.PNG" alt="LED board back showing copper pour and aluminum bottom layer" width="500"/>
</p>

<p align="center">
  <img src="docs/images/pcb_led_panel_assembled.jpg" alt="All 4 LED boards assembled in 2x2 arrangement" width="700"/>
</p>

### 4. Sensor Board

A minimal standalone board carrying just the AHT20 temperature/humidity sensor, connected to the hub over a 4-pin I2C cable. Mounted mid-height on the cabinet wall, away from heat sources, for accurate ambient readings.

<p align="center">
  <img src="docs/images/pcb_sensor_top.jpg" alt="Sensor board top" width="400"/>
  &nbsp;&nbsp;
  <img src="docs/images/sensor_front.PNG" alt="Sensor board KiCad 3D render" width="400"/>
</p>

---

## Web Dashboard

A single self-contained HTML file served directly from ESP32-S3 flash — no internet connection required. Access it at `http://growbox.local` from any device on the same WiFi network.

<p align="center">
  <img src="docs/images/dashboard_full.jpg" alt="Full dashboard screenshot" width="700"/>
</p>
<p align="center">
  <img src="docs/images/dashboard_full.jpg" alt="Dashboard Mobile Login Page" width="700"/>
</p>

<p align="center">
  <img src="docs/images/dashboard_mobile.jpg" alt="Dashboard on mobile" width="300"/>
  &nbsp;&nbsp;
  <img src="docs/images/dashboard_camera.jpg" alt="Dashboard with camera feed active" width="300"/>
  &nbsp;&nbsp;
  <img src="docs/images/dashboard_schedule.jpg" alt="Dashboard light schedule panel" width="300"/>
</p>

| Panel | Description |
|-------|-------------|
| Camera feed | Live MJPEG stream from ESP32-CAM, tap to enable/disable |
| Temperature / humidity | Live readings, session min/max |
| LED brightness | Slider 0–100% with live watt estimation |
| Fan control | Independent intake and exhaust sliders |
| Light schedule | On/off time pickers, 24h visual timeline, enable toggle |
| System status | WiFi RSSI, uptime, PD status, free heap, grow day counter |
| Power summary | Estimated total wall draw |

---

## Camera Feed

The ESP32-CAM mounts on the interior cabinet wall via a 3D printed bracket, angled downward to cover the full plant canopy, connected to the hub over a 4-wire UART cable. It streams on demand only — power-saving start/stop commands are sent as the dashboard opens and closes the feed. Protocol details in [BUILD.md](BUILD.md).

<p align="center">
  <img src="docs/images/camera_mount.jpg" alt="ESP32-CAM mounted inside cabinet" width="500"/>
</p>

<p align="center">
  <img src="docs/images/camera_feed_plants.jpg" alt="Camera feed showing plants" width="600"/>
</p>

---

## Features

- **USB-C PD negotiation** — full FUSB302 driver written from scratch in C. Requests 20V fixed PDO, handles Source_Capabilities, Accept, PS_RDY. Retries every 30 seconds on failure, renegotiates on cable disconnect
- **LED brightness control** — 0–100% PWM via LEDC at 25kHz. Smooth 5-minute sunrise/sunset ramp on schedule transitions. Persisted to NVS
- **Light schedule** — configurable on/off time, SNTP sync, Europe/Brussels timezone, grow day counter from first boot
- **Fan control** — independent intake and exhaust speed 0–100%
- **Environment monitoring** — AHT20 polled every 10 seconds, session min/max for temperature and humidity
- **Camera integration** — ESP32-CAM sends IP over UART on boot, hub proxies MJPEG to browser on demand
- **Web dashboard** — self-contained single HTML file, mobile responsive, no internet dependency
- **WiFi provisioning** — SoftAP captive portal on first boot, saves credentials to NVS, connects automatically after
- **mDNS** — accessible at `growbox.local`, no IP address needed

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

Power draw at each brightness level: see [BUILD.md](BUILD.md#power-budget).

---

## More

- **[BUILD.md](BUILD.md)** — GPIO pinout, wiring, connectors, NVS config, task structure, project layout, and build/flash instructions
- **[BOM.md](BOM.md)** — parts list and cost breakdown

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
