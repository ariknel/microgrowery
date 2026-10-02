#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "cJSON.h"
#include "uart_link.h"
#include "flash_led.h"

static const char *TAG = "uart_link";
static volatile bool s_streaming = false;
static volatile bool s_snapshot_requested = false;

esp_err_t uart_link_init(void)
{
    uart_config_t cfg = {
        .baud_rate = CAM_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(CAM_UART_PORT, 2048, 2048, 0, NULL, 0);
    if (err != ESP_OK) return err;

    err = uart_param_config(CAM_UART_PORT, &cfg);
    if (err != ESP_OK) return err;

    return uart_set_pin(CAM_UART_PORT, CAM_UART_TX_GPIO, CAM_UART_RX_GPIO,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void uart_link_send_cam_ip(const char *ip)
{
    char line[64];
    int len = snprintf(line, sizeof(line), "{\"cam_ip\":\"%s\"}\n", ip);
    uart_write_bytes(CAM_UART_PORT, line, len);
}

/* Framing the hub expects: 0xFF 0xD8 [4-byte big-endian length] [JPEG] 0xFF 0xD9 */
void uart_link_send_frame(camera_fb_t *fb)
{
    uint8_t header[6];
    header[0] = 0xFF;
    header[1] = 0xD8;
    header[2] = (fb->len >> 24) & 0xFF;
    header[3] = (fb->len >> 16) & 0xFF;
    header[4] = (fb->len >> 8) & 0xFF;
    header[5] = fb->len & 0xFF;

    uart_write_bytes(CAM_UART_PORT, (const char *)header, sizeof(header));
    uart_write_bytes(CAM_UART_PORT, (const char *)fb->buf, fb->len);

    uint8_t trailer[2] = { 0xFF, 0xD9 };
    uart_write_bytes(CAM_UART_PORT, (const char *)trailer, sizeof(trailer));
}

/* Blocks until a full newline-terminated line has been read. Shared by the
 * ongoing command loop and the one-shot wait-for-WiFi-creds call at boot. */
static void read_line_blocking(char *line, size_t line_size)
{
    size_t pos = 0;
    for (;;) {
        uint8_t c;
        int n = uart_read_bytes(CAM_UART_PORT, &c, 1, portMAX_DELAY);
        if (n <= 0) continue;

        if (c == '\n') {
            line[pos] = '\0';
            if (pos > 0) return;
            pos = 0; /* blank line, keep waiting */
        } else if (pos < line_size - 1) {
            line[pos++] = (char)c;
        }
    }
}

static void handle_command(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) return;

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (cJSON_IsString(cmd)) {
        if (strcmp(cmd->valuestring, "stream") == 0) {
            cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "state");
            s_streaming = cJSON_IsNumber(st) && st->valueint != 0;
            ESP_LOGI(TAG, "stream %s", s_streaming ? "started" : "stopped");
        } else if (strcmp(cmd->valuestring, "snapshot") == 0) {
            s_snapshot_requested = true;
            ESP_LOGI(TAG, "snapshot requested");
        } else if (strcmp(cmd->valuestring, "flash") == 0) {
            flash_led_trigger();
            ESP_LOGI(TAG, "flash LED triggered");
        }
    }
    cJSON_Delete(root);
}

void uart_link_command_task(void *arg)
{
    char line[200];
    for (;;) {
        read_line_blocking(line, sizeof(line));
        handle_command(line);
    }
}

void uart_link_wait_for_wifi_creds(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    char line[200];
    ssid_out[0] = '\0';
    pass_out[0] = '\0';

    for (;;) {
        read_line_blocking(line, sizeof(line));

        cJSON *root = cJSON_Parse(line);
        if (!root) continue;

        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
        if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
            cJSON *pass = cJSON_GetObjectItemCaseSensitive(root, "wifi_pass");
            strncpy(ssid_out, ssid->valuestring, ssid_len - 1);
            strncpy(pass_out, cJSON_IsString(pass) ? pass->valuestring : "", pass_len - 1);
            cJSON_Delete(root);
            return;
        }
        cJSON_Delete(root);
    }
}

bool uart_link_streaming_enabled(void)
{
    return s_streaming;
}

bool uart_link_consume_snapshot_request(void)
{
    if (s_snapshot_requested) {
        s_snapshot_requested = false;
        return true;
    }
    return false;
}
