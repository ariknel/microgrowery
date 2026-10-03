#include <math.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "uart_fan.h"
#include "state.h"

static const char *TAG = "uart_fan";

esp_err_t uart_fan_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_FAN_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_FAN_PORT, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    err = uart_param_config(UART_FAN_PORT, &cfg);
    if (err != ESP_OK) return err;

    /* Deliberately no uart_set_pin() call here — that keeps UART0 on its
     * default/ROM-assigned GPIOs instead of risking a remap to the wrong
     * pins before the hub's own schematic is in the repo to confirm
     * against. Nothing else in this firmware touches UART0. */
    ESP_LOGI(TAG, "listening for fan controller telemetry on UART0");
    return ESP_OK;
}

/* Same newline-terminated line framing as the hub<->cam link (see
 * uart_cam.c) — no length prefix, no checksum, just a JSON object per
 * line. This link is lower-stakes (telemetry only, nothing acts on it in
 * real time) so that simplicity is enough here too. */
static void read_line_blocking(char *line, size_t line_size)
{
    size_t pos = 0;
    for (;;) {
        uint8_t c;
        int n = uart_read_bytes(UART_FAN_PORT, &c, 1, portMAX_DELAY);
        if (n <= 0) continue;

        if (c == '\n') {
            line[pos] = '\0';
            if (pos > 0) return;
            pos = 0; /* blank line, keep waiting */
        } else if (c != '\r' && pos < line_size - 1) {
            line[pos++] = (char)c;
        }
    }
}

/* Expects {"temps":[f,f,f,f],"duty":[n,n,n,n]} — a missing/non-numeric
 * entry becomes NAN (temps) or 0 (duty) rather than silently keeping the
 * previous value, so a malformed line can't look like a real reading.
 * Anything that isn't valid JSON (including the Nano's own boot banner,
 * or ROM boot-time noise on this UART) is ignored, same as every other
 * UART-JSON link in this project. */
static void handle_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) return;

    cJSON *temps = cJSON_GetObjectItemCaseSensitive(root, "temps");
    cJSON *duty = cJSON_GetObjectItemCaseSensitive(root, "duty");

    if (cJSON_IsArray(temps) && cJSON_IsArray(duty)) {
        state_lock();
        for (int i = 0; i < NUM_FAN_ZONES; i++) {
            cJSON *t = cJSON_GetArrayItem(temps, i);
            cJSON *d = cJSON_GetArrayItem(duty, i);
            g_state.fan_ctrl_temp_c[i] = (t && cJSON_IsNumber(t)) ? (float)t->valuedouble : NAN;
            g_state.fan_ctrl_duty[i] = (d && cJSON_IsNumber(d)) ? (uint8_t)d->valueint : 0;
        }
        g_state.fan_ctrl_online = true;
        state_unlock();
    }

    cJSON_Delete(root);
}

static void uart_fan_task(void *arg)
{
    char line[160];
    for (;;) {
        read_line_blocking(line, sizeof(line));
        handle_line(line);
    }
}

void uart_fan_task_start(UBaseType_t priority, BaseType_t core_id)
{
    xTaskCreatePinnedToCore(uart_fan_task, "uart_fan_task", 3072, NULL, priority, NULL, core_id);
}
