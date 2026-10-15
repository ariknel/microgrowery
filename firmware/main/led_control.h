#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LED_GPIO   11
#define FAN1_GPIO  12   /* intake */
#define FAN2_GPIO  13   /* exhaust */

#define LED_PANEL_MAX_WATTS 65.0f

/* Everything on VBUS besides the LED panels: ESP32, camera, and the fans on
 * the 12V buck. Subtracted from the PD contract before sizing LED output. */
#define NON_LED_BUDGET_MW 10000

esp_err_t led_control_init(void);

/* User setpoints (0-100), persisted. The brightness setpoint is only driven
 * while the schedule has the lights on. */
esp_err_t led_set_brightness(uint8_t percent);
esp_err_t fan_set_speed(uint8_t fan_id, uint8_t percent); /* fan_id: 1 or 2 */

/* What the schedule wants the LED PWM at right now (ramps, on, off). Not
 * persisted, doesn't touch the setpoint. */
void led_set_output(uint8_t percent);

/* Power the PD contract can feed, in mW; 0 when there's no 20V contract.
 * LEDs and fans stay off at 0, and LED output is capped to fit. */
void led_set_power_budget(uint32_t contract_mw);

float led_watts_for(uint8_t percent);
