#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_config.h"

static const char *TAG = "nvs_config";
#define NVS_NAMESPACE "growbox"

static nvs_handle_t s_handle;

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, reformatting");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_config_get_str(const char *key, char *out, size_t out_len, const char *def)
{
    size_t len = out_len;
    esp_err_t err = nvs_get_str(s_handle, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(out, def, out_len - 1);
        out[out_len - 1] = '\0';
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "get_str(%s) failed: %s, using default", key, esp_err_to_name(err));
        strncpy(out, def, out_len - 1);
        out[out_len - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t nvs_config_get_u8(const char *key, uint8_t *out, uint8_t def)
{
    esp_err_t err = nvs_get_u8(s_handle, key, out);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "get_u8(%s) failed: %s, using default", key, esp_err_to_name(err));
        }
        *out = def;
    }
    return ESP_OK;
}

esp_err_t nvs_config_get_u16(const char *key, uint16_t *out, uint16_t def)
{
    esp_err_t err = nvs_get_u16(s_handle, key, out);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "get_u16(%s) failed: %s, using default", key, esp_err_to_name(err));
        }
        *out = def;
    }
    return ESP_OK;
}

esp_err_t nvs_config_get_u32(const char *key, uint32_t *out, uint32_t def)
{
    esp_err_t err = nvs_get_u32(s_handle, key, out);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "get_u32(%s) failed: %s, using default", key, esp_err_to_name(err));
        }
        *out = def;
    }
    return ESP_OK;
}

esp_err_t nvs_config_set_str(const char *key, const char *val)
{
    esp_err_t err = nvs_set_str(s_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_str(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

esp_err_t nvs_config_set_u8(const char *key, uint8_t val)
{
    esp_err_t err = nvs_set_u8(s_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_u8(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

esp_err_t nvs_config_set_u16(const char *key, uint16_t val)
{
    esp_err_t err = nvs_set_u16(s_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_u16(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}

esp_err_t nvs_config_set_u32(const char *key, uint32_t val)
{
    esp_err_t err = nvs_set_u32(s_handle, key, val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_u32(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }
    return nvs_commit(s_handle);
}
