#pragma once

#include <stdint.h>
#include "esp_err.h"

/* NVS namespace: "growbox". All keys below live in it.
 *
 * Key            Type    Default
 * wifi_ssid      string  --
 * wifi_pass      string  --
 * led_brightness u8      70
 * fan1_speed     u8      50
 * fan2_speed     u8      60
 * sched_on       u16     360  (06:00)
 * sched_off      u16     1320 (22:00)
 * sched_enabled  u8      1
 * first_boot_ts  u32     0
 * firmware_ver   string  "1.0.0"
 * auth_code      string  "4712"
 */

esp_err_t nvs_config_init(void);

esp_err_t nvs_config_get_str(const char *key, char *out, size_t out_len, const char *def);
esp_err_t nvs_config_get_u8(const char *key, uint8_t *out, uint8_t def);
esp_err_t nvs_config_get_u16(const char *key, uint16_t *out, uint16_t def);
esp_err_t nvs_config_get_u32(const char *key, uint32_t *out, uint32_t def);

esp_err_t nvs_config_set_str(const char *key, const char *val);
esp_err_t nvs_config_set_u8(const char *key, uint8_t val);
esp_err_t nvs_config_set_u16(const char *key, uint16_t val);
esp_err_t nvs_config_set_u32(const char *key, uint32_t val);
