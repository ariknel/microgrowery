#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "aht20.h"

static const char *TAG = "aht20";
static i2c_port_t s_port;

static esp_err_t aht20_write(const uint8_t *data, size_t len)
{
    return i2c_master_write_to_device(s_port, AHT20_I2C_ADDR, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t aht20_read_raw(uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(s_port, AHT20_I2C_ADDR, data, len, pdMS_TO_TICKS(100));
}

esp_err_t aht20_init(i2c_port_t port)
{
    s_port = port;

    /* AHT20 init command: 0xBE 0x08 0x00 */
    uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
    esp_err_t err = aht20_write(init_cmd, sizeof(init_cmd));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init write failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t aht20_read(float *temp_c, float *humidity_pct)
{
    uint8_t trigger_cmd[3] = { 0xAC, 0x33, 0x00 };
    esp_err_t err = aht20_write(trigger_cmd, sizeof(trigger_cmd));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "trigger write failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t raw[6] = { 0 };
    err = aht20_read_raw(raw, sizeof(raw));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (raw[0] & 0x80) {
        /* busy bit still set, measurement not ready */
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t raw_hum = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    uint32_t raw_temp = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

    *humidity_pct = ((float)raw_hum / 1048576.0f) * 100.0f;
    *temp_c = (((float)raw_temp / 1048576.0f) * 200.0f) - 50.0f;

    return ESP_OK;
}
