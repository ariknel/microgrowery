/* GrowBox LED cooling fan controller — Arduino Nano
 * Arik Nel
 * Independent thermal safety loop for the 8x 5015 blowers cooling the LED
 * heatsinks (see ../BUILD.md#thermal-analysis--led-board-cooling). Runs on
 * its own Nano specifically so fan control keeps working even if the
 * ESP32-S3 hub or ESP32-CAM hang, crash, or are mid-reboot — it has no
 * dependency on either board.
 *
 * 4 independent zones, one per LED board: each zone has its own NTC
 * (mounted directly on that board's PCB, not on a heatsink, so it reads
 * that specific board's temperature rather than general airflow) and its
 * own MOSFET switching that board's 2 fans together. A board running
 * hotter than the other 3 only speeds up its own fans, not all 8 — unlike
 * a single shared-output design, one board with worse airflow doesn't
 * drag the other three's fans up with it.
 *
 * Wiring (x4, one set per zone):
 *   NTC analog input — voltage divider:
 *     5V --- NTC --- (node, to Nano Ax) --- SERIES_RESISTOR_OHMS --- GND
 *   MOSFET gate — Nano PWM pin, through a ~220ohm series resistor, to a
 *     logic-level N-channel MOSFET gate (e.g. IRLZ44N). MOSFET source to
 *     GND, drain to that board's 2 fans' common return; fans' + goes
 *     straight to the 12V rail. Add a ~10k gate pull-down resistor (gate
 *     to GND) on each MOSFET so it defaults OFF (not floating) during
 *     Nano power-on/reset, before setup() takes over — a floating gate
 *     can sit the MOSFET in its lossy linear region and overheat it,
 *     which matters more here than losing a few hundred ms of cooling.
 *
 * Reports to the hub over the same hardware Serial used for USB upload
 * (Nano pins 0/RX, 1/TX) — wired to the ESP32-S3 hub's J14 UART0 debug
 * header (hub TX->Nano RX not needed, this is send-only: the hub never
 * talks back). That header carries nothing else — the hub flashes over
 * native USB and its console goes out over USB-Serial-JTAG, not UART0, so
 * it's free. Unplug this link before re-uploading the sketch over USB, same
 * as unplugging anything else wired to pins 0/1.
 *
 * No external libraries required — stock Arduino IDE, board "Arduino
 * Nano", upload like any sketch.
 */

#include <Arduino.h>

/* ---------------- configuration ---------------- */

#define NUM_ZONES 4

/* zone index 0..3 = LED board 1..4 — keep this order consistent with
 * however the boards are physically labeled in the cabinet, so "zone 2
 * running hot" on the serial log actually points at the right board. */
const uint8_t SENSOR_PINS[NUM_ZONES] = { A0, A1, A2, A3 };
const uint8_t FAN_PWM_PINS[NUM_ZONES] = { 5, 6, 9, 10 };

/* NTC voltage divider: 5V -> NTC -> (sense node) -> SERIES_RESISTOR -> GND.
 * Part on hand: HNTC-104F3950FB, 100k @25C, B=3950, +/-1% (LCSC C52204613).
 * 47k series resistor centers the divider reasonably across the 35-55C fan
 * curve (~327/1023 at 25C, ~626/1023 at 55C) — change it here if the real
 * build ends up using a different value. */
#define SERIES_RESISTOR_OHMS 47000.0
#define NTC_NOMINAL_OHMS     100000.0
#define NTC_NOMINAL_C        25.0
#define NTC_BETA             3950.0
#define ADC_MAX               1023.0

/* Fan speed curve, per zone. Below FAN_START_C that zone's fans are fully
 * off; duty ramps linearly from MIN_DUTY at FAN_START_C to 255 (full) at
 * FAN_FULL_C and stays at full above that. FAN_OFF_HYST_C keeps the curve
 * from chattering right at the start threshold. On-PCB NTCs read closer to
 * the actual board temperature than a heatsink-mounted sensor would, but
 * these numbers are still placeholders — tune them against a real
 * thermocouple on the board, same as every other number in the thermal
 * analysis this design is based on. */
#define FAN_START_C   35.0
#define FAN_FULL_C    55.0
#define FAN_OFF_HYST_C 3.0
#define FAN_MIN_DUTY   90   /* out of 255 — enough to spin a small blower reliably, not stall at a too-low PWM duty */

/* A sensor reading this far outside a plausible range (wiring fault, open
 * circuit, short) is treated as a FAILURE for that zone, not as "cold" —
 * that zone fails toward fans-on, not fans-off, since this is a cooling
 * safety loop. Other zones are unaffected. */
#define SENSOR_FAULT_LOW_C  -20.0
#define SENSOR_FAULT_HIGH_C 120.0

#define SERIAL_BAUD        9600
#define REPORT_INTERVAL_MS 2000
#define SAMPLE_INTERVAL_MS 500

/* Matches the JSON-line-over-UART convention already used for the hub<->cam
 * link (see ../firmware/main/uart_cam.c) — one line, newline-terminated.
 * A faulted sensor reports as JSON null rather than a made-up number. */
void reportToHub(float temps[NUM_ZONES], uint8_t duties[NUM_ZONES])
{
    Serial.print(F("{\"temps\":["));
    for (uint8_t z = 0; z < NUM_ZONES; z++) {
        if (isnan(temps[z])) Serial.print(F("null"));
        else Serial.print(temps[z], 1);
        if (z < NUM_ZONES - 1) Serial.print(',');
    }
    Serial.print(F("],\"duty\":["));
    for (uint8_t z = 0; z < NUM_ZONES; z++) {
        Serial.print(duties[z]);
        if (z < NUM_ZONES - 1) Serial.print(',');
    }
    Serial.println(F("]}"));
}

/* ---------------- state ---------------- */

bool zoneFansRunning[NUM_ZONES] = { false, false, false, false };
unsigned long lastSampleMs = 0;
unsigned long lastReportMs = 0;

float readTempC(uint8_t pin)
{
    int raw = analogRead(pin);

    /* Open circuit (NTC unplugged) reads near ADC_MAX; a dead short reads
     * near 0 — both are physically impossible real temperatures, so they're
     * the easy case to catch before the Steinhart-Hart math produces a
     * plausible-looking but meaningless number. */
    if (raw <= 1 || raw >= (int)ADC_MAX - 1) {
        return NAN;
    }

    float resistance = SERIES_RESISTOR_OHMS * (ADC_MAX / (float)raw - 1.0);

    /* Simplified beta equation (Steinhart-Hart with only the beta term) —
     * accurate enough for a fan-curve input, not lab-grade calibration. */
    float steinhart = resistance / NTC_NOMINAL_OHMS;
    steinhart = log(steinhart);
    steinhart /= NTC_BETA;
    steinhart += 1.0 / (NTC_NOMINAL_C + 273.15);
    steinhart = 1.0 / steinhart;
    steinhart -= 273.15;

    if (steinhart < SENSOR_FAULT_LOW_C || steinhart > SENSOR_FAULT_HIGH_C) {
        return NAN;
    }
    return steinhart;
}

uint8_t dutyForZone(uint8_t zone, float tempC)
{
    /* NAN means this zone's sensor is faulty — fail safe to full speed
     * for this zone only, rather than trusting a bogus "cold" reading. */
    if (isnan(tempC)) {
        zoneFansRunning[zone] = true;
        return 255;
    }

    float startThreshold = zoneFansRunning[zone] ? (FAN_START_C - FAN_OFF_HYST_C) : FAN_START_C;

    if (tempC < startThreshold) {
        zoneFansRunning[zone] = false;
        return 0;
    }
    zoneFansRunning[zone] = true;

    if (tempC >= FAN_FULL_C) return 255;

    float span = FAN_FULL_C - FAN_START_C;
    float frac = (tempC - FAN_START_C) / span;
    if (frac < 0) frac = 0;
    int duty = FAN_MIN_DUTY + (int)(frac * (255 - FAN_MIN_DUTY));
    if (duty > 255) duty = 255;
    if (duty < FAN_MIN_DUTY) duty = FAN_MIN_DUTY;
    return (uint8_t)duty;
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    for (uint8_t z = 0; z < NUM_ZONES; z++) {
        pinMode(FAN_PWM_PINS[z], OUTPUT);
        analogWrite(FAN_PWM_PINS[z], 0); /* fans off until the first real reading */
    }
    Serial.println(F("GrowBox fan controller starting — 4 independent zones"));
}

void loop()
{
    unsigned long now = millis();
    if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
    lastSampleMs = now;

    float temps[NUM_ZONES];
    uint8_t duties[NUM_ZONES];

    for (uint8_t z = 0; z < NUM_ZONES; z++) {
        temps[z] = readTempC(SENSOR_PINS[z]);
        duties[z] = dutyForZone(z, temps[z]);
        analogWrite(FAN_PWM_PINS[z], duties[z]);
    }

    if (now - lastReportMs >= REPORT_INTERVAL_MS) {
        lastReportMs = now;
        reportToHub(temps, duties);
    }
}
