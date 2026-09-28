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
```

---

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
