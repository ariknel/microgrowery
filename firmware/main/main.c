#include <stdbool.h>
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

/* Logs which 7-bit addresses ACK a zero-length write — standard I2C scan
 * technique. Run once at boot so a dead bus (no pull-ups, nothing
 * populated, a solder bridge) is immediately visible in the log instead
 * of showing up only as two unrelated-looking timeout errors (AHT20,
 * FUSB302) further down. */
static void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "I2C bus scan:");
    bool any = false;
    uint8_t dummy = 0; /* zero-length write still needs a non-NULL buffer
                         * pointer on this ESP-IDF version, even though
                         * nothing is actually sent — a NULL pointer fails
                         * before the bus is even touched ("null address
                         * error"), which looks identical to every address
                         * silently NACKing unless you know to check for it. */
    for (uint8_t addr = 1; addr < 0x78; addr++) {
        if (i2c_master_write_to_device(I2C_PORT, addr, &dummy, 0, pdMS_TO_TICKS(100)) == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02X ACKed", addr);
            any = true;
        }
    }
    if (!any) {
        ESP_LOGW(TAG, "  nothing ACKed — hub board has its own 10k pull-ups, so this "
                       "isn't a missing-pull-up situation; check SDA/SCL solder joints, "
                       "GPIO8/9 wiring, and whether anything on the bus is actually "
                       "powered");
    }
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
    i2c_bus_scan();

    /* Non-fatal on purpose: the sensor board is a separate, optional
     * connector (see BUILD.md), and sensor_task already treats an ongoing
     * read failure as a warning, not a crash. The one-time init used to be
     * ESP_ERROR_CHECK'd too, which meant booting with the sensor board
     * unplugged (e.g. during hub-only bring-up/testing) panic-looped the
     * whole controller before it ever got to WiFi/HTTP. g_state.sensor_valid
     * just stays false — /api/sensors and /api/status already guard on it. */
    esp_err_t aht_err = aht20_init(I2C_PORT);
    if (aht_err != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 init failed (%s) — continuing without it", esp_err_to_name(aht_err));
    }

    ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(schedule_init());
    ESP_ERROR_CHECK(uart_cam_init());
    uart_cam_task_start(4, 1);

    /* uart_fan (CH32F4 fan-controller telemetry) is disabled for now — it
     * wants UART0, and the debug console was just moved onto UART0 too
     * (see sdkconfig.defaults) after the native USB-Serial-JTAG console
     * stopped enumerating (suspected cold joint on the USB-C D+/D-, GPIO19/
     * 20). Both can't own UART0 at once. Re-enable once either JTAG is
     * fixed (console goes back to USB-Serial-JTAG, UART0 freed up again) or
     * the fan controller gets rewired to a different UART. */
    /* ESP_ERROR_CHECK(uart_fan_init());
     * uart_fan_task_start(3, 1); */

    /* Core 1, not core 0 — a PD source only resends Source_Capabilities a
     * handful of times right after attach before giving up and Hard
     * Resetting (a real VBUS drop, which power-cycles this board since it
     * has no backup supply). main_task runs app_main on core 0 and the
     * wifi_manager_start_station()/http_server_start() calls below block
     * for a while, starving pd_task of core 0 time right when it matters
     * most — every boot log showed "attached source" only appearing
     * ~1000ms in, almost certainly after the source's own narrow
     * capabilities-broadcast window had already closed. */
    pd_task_start(I2C_PORT, 5, 1);
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 2048, NULL, 3, NULL, 0);

    ESP_ERROR_CHECK(wifi_manager_start_station());
    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "microgrowery controller boot complete");
}
