# GrowBox Firmware

ESP-IDF firmware for a DIY microgrowery hub controller built around the ESP32-S3. Controls LED grow lighting, ventilation fans, monitors environment, negotiates USB-C PD power, and serves a local web dashboard over WiFi.

---

## Hardware

### Main Controller
- **MCU:** ESP32-S3 (QFN-56, bare SoC) + GD25WQ64ESIGR 8MB NOR flash
- **Power:** USB-C PD via FUSB302 — negotiates 20V from any PD 3.0 compliant charger
- **Camera:** ESP32-CAM module (separate board, communicates via UART1)

### LED Panel
- 4× driver boards, each with 3× TX6120 buck constant-current LED drivers
- 12 strings × 5× XL-3030WWC-1W-3V warm white LEDs = 60 LEDs total
- ~65W at 100% brightness, powered at 20V from USB-C PD
- Single PWM DIM signal from ESP32-S3 LEDC peripheral controls all 12 drivers simultaneously

### Peripheral Map

| Peripheral | Interface | GPIO |
|-----------|-----------|------|
| FUSB302 PD controller | I2C | SDA=8, SCL=9, INT=10 |
| AHT20 temp/humidity | I2C (shared bus) | SDA=8, SCL=9 |
| LED panel DIM | LEDC CH0 25kHz | GPIO11 |
| Intake fan | LEDC CH1 25kHz | GPIO12 |
| Exhaust fan | LEDC CH2 25kHz | GPIO13 |
| ESP32-CAM | UART1 921600 baud | RX=17, TX=18 |
| TFT display | SPI | MOSI=4, MISO=5, CLK=6, CS=7 |
| TFT DC | GPIO | GPIO14 |
| TFT RST | GPIO | GPIO21 |
| TFT backlight | GPIO | GPIO38 |
| Touch CS | SPI | GPIO39 |
| Touch IRQ | GPIO | GPIO40 |
| USB Serial/JTAG | — | D-=19, D+=20 |

### Power Architecture

```
USB-C PD charger (100W GaN recommended)
        │
        ▼
FUSB302 negotiates 20V fixed PDO
        │
        ├──► 20V rail ──► 4× LED driver boards (JST VH 2-pin, 20AWG per board)
        │
        ├──► SY8120B buck ──► 3.3V ──► ESP32-S3, AHT20, FUSB302
        │
        └──► dedicated 3.3V regulator ──► ESP32-CAM
```

---

## Features

- **USB-C PD negotiation** — full FUSB302 driver from scratch, requests 20V fixed PDO, handles Accept/PS_RDY sequence, retries every 30 seconds on failure, detects cable disconnect and renegotiates
- **LED brightness control** — 0-100% PWM via web dashboard, smooth 5-minute sunrise/sunset ramp on schedule transitions, settings persisted to NVS
- **Light schedule** — configurable on/off time in NVS, SNTP time sync, timezone Europe/Brussels, day counter from first boot timestamp
- **Fan control** — independent intake and exhaust speed 0-100%, negative pressure maintained by running exhaust higher than intake at all times
- **Environment monitoring** — AHT20 temperature (°C) and humidity (%), session min/max tracking, polled every 10 seconds
- **Camera integration** — ESP32-CAM sends its IP over UART on boot, ESP32-S3 proxies MJPEG stream to browser on demand via /stream endpoint, sends start/stop commands to camera over UART
- **Web dashboard** — single-page app served from ESP32-S3 flash, dark theme, shows all sensor readings, controls, live camera feed, estimated wattage, PD status, uptime, grow day counter
- **WiFi provisioning** — on first boot with no credentials in NVS, starts SoftAP GrowBox-Setup and serves captive portal for WiFi setup, saves credentials to NVS and restarts
- **mDNS** — accessible at `growbox.local` on local network after WiFi connect

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
| POST | `/api/schedule` | Set light schedule `{"on":"06:00","off":"22:00","enabled":true}` |
| GET | `/api/camera` | Camera IP and stream URL |
| GET | `/stream` | MJPEG stream proxy from ESP32-CAM |
| POST | `/api/config/wifi` | Update WiFi credentials, triggers restart |

---

## Project Structure

```
growbox-firmware/
├── main/
│   ├── main.c              # app_main, init sequence, task spawning
│   ├── fusb302.c/.h        # FUSB302 I2C register driver
│   ├── fusb302_pd.c/.h     # USB PD protocol state machine
│   ├── aht20.c/.h          # AHT20 temperature/humidity driver
│   ├── led_control.c/.h    # LEDC LED and fan PWM control
│   ├── schedule.c/.h       # Light schedule logic and SNTP
│   ├── wifi_manager.c/.h   # WiFi, mDNS, captive portal
│   ├── http_server.c/.h    # All HTTP endpoint handlers
│   ├── uart_cam.c/.h       # ESP32-CAM UART protocol
│   ├── nvs_config.c/.h     # NVS read/write helpers
│   ├── state.c/.h          # Global state struct and mutex
│   ├── dashboard.h         # Embedded dashboard HTML string
│   └── CMakeLists.txt
├── partitions.csv          # Custom partition table
├── sdkconfig.defaults      # Default SDK configuration
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
- `pd_event_queue` — depth 8, carries FUSB302 interrupt events from GPIO ISR to pd_task
- `sensor_data_queue` — depth 2, carries latest AHT20 readings

All shared state lives in a single global struct protected by one FreeRTOS mutex.

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
| `sched_on` | uint16 | 360 | Light on time (minutes since midnight) |
| `sched_off` | uint16 | 1320 | Light off time (minutes since midnight) |
| `sched_enabled` | uint8 | 1 | Schedule active flag |
| `first_boot_ts` | uint32 | 0 | Unix timestamp of first boot for grow day counter |
| `firmware_ver` | string | "1.0.0" | Firmware version string |

---

## Building and Flashing

### Prerequisites

- ESP-IDF v5.x installed and sourced
- USB-C cable connected to hub board (USB Serial/JTAG on GPIO19/GPIO20)

### Build

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

On first boot with no WiFi credentials stored, the device starts a SoftAP:

```
SSID:     GrowBox-Setup
Password: growbox123
URL:      http://192.168.4.1
```

Enter your WiFi credentials via the captive portal. The device restarts and connects. Access the dashboard at:

```
http://growbox.local
```

### JTAG Debug

USB Serial/JTAG is available natively on GPIO19/GPIO20. Use OpenOCD with the ESP32-S3 target:

```bash
openocd -f board/esp32s3-builtin.cfg
```

---

## ESP32-CAM Setup

The ESP32-CAM runs independently on the same WiFi network. On boot it sends its IP to the hub over UART1 at 921600 baud:

```json
{"cam_ip":"192.168.1.x"}
```

The hub stores this IP and uses it to proxy the MJPEG stream to the web dashboard. Flash the ESP32-CAM with standard ESP32-CAM-WebServer firmware configured for 921600 baud UART reporting and QVGA (320×240) JPEG output.

---

## Wiring the LED Boards

Each of the 4 LED driver boards connects to the hub via:

- **JST VH 2-pin** — 20V power + GND (20AWG minimum)
- **2-pin signal** — DIM_PWM + GND (26AWG)

Power is star-topology from the hub to each board independently — do not daisy chain 20V through boards. DIM_PWM signal may daisy chain as it carries only milliamps.

Inter-string wiring within each board uses 26AWG UL2468 at 2.54mm pitch. Each string carries 340mA — well within 26AWG 1A rating.

---

## Grow Schedule Recommendations

| Crop | Light on | Light off | Brightness |
|------|---------|----------|-----------|
| Seedlings | 06:00 | 22:00 (16h) | 40% |
| Vegetative | 06:00 | 00:00 (18h) | 70% |
| Autoflower full cycle | 06:00 | 00:00 (18h) | 75% |
| Photoperiod flower | 08:00 | 20:00 (12h) | 85% |
| Peppers / herbs | 06:00 | 22:00 (16h) | 65% |

Estimated wall draw at different brightness levels:

| Brightness | LED power | Total estimated |
|-----------|----------|----------------|
| 50% | 32.5W | ~37W |
| 65% | 42.3W | ~47W |
| 75% | 48.8W | ~54W |
| 100% | 65W | ~70W |

---

## License

MIT License. See LICENSE file.

---

## Author

Arik — IoT & Electronics Engineering Student, Belgium
