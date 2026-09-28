#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LED_GPIO   11
#define FAN1_GPIO  12   /* intake */
#define FAN2_GPIO  13   /* exhaust */

#define LED_PANEL_MAX_WATTS 65.0f

esp_err_t led_control_init(void);

/* percent: 0-100 */
esp_err_t led_set_brightness(uint8_t percent);
esp_err_t fan_set_speed(uint8_t fan_id, uint8_t percent); /* fan_id: 1 or 2 */

float led_watts_for(uint8_t percent);
