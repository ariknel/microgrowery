#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

#define AHT20_I2C_ADDR 0x38

esp_err_t aht20_init(i2c_port_t port);
esp_err_t aht20_read(float *temp_c, float *humidity_pct);
