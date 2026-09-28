#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t schedule_init(void);

/* on_min/off_min: minutes since midnight (0-1439). */
esp_err_t schedule_set(uint16_t on_min, uint16_t off_min, bool enabled);

uint32_t schedule_grow_day(void);
