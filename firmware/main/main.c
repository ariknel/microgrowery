#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "state.h"
#include "nvs_config.h"
#include "auth.h"
#include "aht20.h"
#include "fusb302_pd.h"
#include "led_control.h"
#include "schedule.h"
#include "wifi_manager.h"
#include "uart_cam.h"
#include "uart_fan.h"
#include "http_server.h"

static const char *TAG = "main";

#define I2C_PORT       I2C_NUM_0
#define I2C_SDA_GPIO   8
#define I2C_SCL_GPIO   9
#define I2C_FREQ_HZ    400000

#define SENSOR_POLL_INTERVAL_MS 10000

static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static void sensor_task(void *arg)
{
    for (;;) {
        float t, h;
        if (aht20_read(&t, &h) == ESP_OK) {
            state_lock();
            g_state.temp_c = t;
            g_state.humidity_pct = h;
            g_state.sensor_valid = true;
            if (t < g_state.temp_min) g_state.temp_min = t;
            if (t > g_state.temp_max) g_state.temp_max = t;
            if (h < g_state.humidity_min) g_state.humidity_min = h;
            if (h > g_state.humidity_max) g_state.humidity_max = h;
            state_unlock();
        } else {
            ESP_LOGW(TAG, "AHT20 read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_config_init());
    state_init();
    ESP_ERROR_CHECK(auth_init());

    if (wifi_manager_needs_provisioning()) {
        ESP_LOGW(TAG, "no WiFi credentials stored, starting provisioning portal");
        wifi_manager_run_provisioning(); /* never returns */
        return;
    }

    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(aht20_init(I2C_PORT));
    ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(schedule_init());
    ESP_ERROR_CHECK(uart_cam_init());
    uart_cam_task_start(4, 1);
    ESP_ERROR_CHECK(uart_fan_init());
    uart_fan_task_start(3, 1);
    pd_task_start(I2C_PORT, 5, 0);
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 2048, NULL, 3, NULL, 0);

    ESP_ERROR_CHECK(wifi_manager_start_station());
    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "microgrowery controller boot complete");
}
