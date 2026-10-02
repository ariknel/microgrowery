#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "uart_cam.h"
#include "state.h"

static const char *TAG = "uart_cam";

typedef struct {
    uint8_t *buf;
    size_t len;
} cam_frame_t;

static QueueHandle_t s_frame_queue;

esp_err_t uart_cam_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_CAM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_CAM_PORT, UART_CAM_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    err = uart_param_config(UART_CAM_PORT, &cfg);
    if (err != ESP_OK) return err;

    err = uart_set_pin(UART_CAM_PORT, UART_CAM_TX_GPIO, UART_CAM_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    s_frame_queue = xQueueCreate(UART_CAM_FRAME_QUEUE_DEPTH, sizeof(cam_frame_t));
    return ESP_OK;
}

static esp_err_t send_cmd(const char *json_line)
{
    int written = uart_write_bytes(UART_CAM_PORT, json_line, strlen(json_line));
    return written >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t uart_cam_request_stream_start(void) { return send_cmd("{\"cmd\":\"stream\",\"state\":1}\n"); }
esp_err_t uart_cam_request_stream_stop(void)  { return send_cmd("{\"cmd\":\"stream\",\"state\":0}\n"); }
esp_err_t uart_cam_request_snapshot(void)     { return send_cmd("{\"cmd\":\"snapshot\"}\n"); }
esp_err_t uart_cam_request_flash(void)        { return send_cmd("{\"cmd\":\"flash\"}\n"); }

esp_err_t uart_cam_send_wifi_creds(const char *ssid, const char *pass)
{
    char line[200];
    int len = snprintf(line, sizeof(line), "{\"wifi_ssid\":\"%s\",\"wifi_pass\":\"%s\"}\n", ssid, pass);
    if (len < 0 || (size_t)len >= sizeof(line)) {
        ESP_LOGW(TAG, "wifi creds line too long, not sending");
        return ESP_ERR_INVALID_SIZE;
    }
    return send_cmd(line);
}

static esp_err_t read_exact(uint8_t *buf, size_t len, TickType_t timeout)
{
    size_t got = 0;
    TickType_t deadline = xTaskGetTickCount() + timeout;
    while (got < len) {
        TickType_t remaining = deadline - xTaskGetTickCount();
        if ((int32_t)remaining <= 0) return ESP_ERR_TIMEOUT;
        int n = uart_read_bytes(UART_CAM_PORT, buf + got, len - got, remaining);
        if (n < 0) return ESP_FAIL;
        got += n;
    }
    return ESP_OK;
}

static void handle_json_line(uint8_t first_byte)
{
    char line[128];
    size_t pos = 0;
    line[pos++] = (char)first_byte;

    while (pos < sizeof(line) - 1) {
        uint8_t c;
        if (read_exact(&c, 1, pdMS_TO_TICKS(200)) != ESP_OK) break;
        if (c == '\n') break;
        line[pos++] = (char)c;
    }
    line[pos] = '\0';

    cJSON *root = cJSON_Parse(line);
    if (!root) return;

    cJSON *ip = cJSON_GetObjectItemCaseSensitive(root, "cam_ip");
    if (cJSON_IsString(ip) && ip->valuestring) {
        state_lock();
        strncpy(g_state.cam_ip, ip->valuestring, CAM_IP_MAXLEN - 1);
        g_state.cam_ip[CAM_IP_MAXLEN - 1] = '\0';
        g_state.cam_online = true;
        state_unlock();
        ESP_LOGI(TAG, "camera IP: %s", ip->valuestring);
    }
    cJSON_Delete(root);
}

static bool try_read_frame(void)
{
    uint8_t marker;
    if (read_exact(&marker, 1, pdMS_TO_TICKS(1000)) != ESP_OK) return false;

    if (marker == '{') {
        handle_json_line(marker);
        return true;
    }
    if (marker != 0xFF) return true; /* stray byte, resync */

    uint8_t soi;
    if (read_exact(&soi, 1, pdMS_TO_TICKS(200)) != ESP_OK) return true;
    if (soi != 0xD8) return true; /* not a frame start */

    uint8_t len_bytes[4];
    if (read_exact(len_bytes, 4, pdMS_TO_TICKS(500)) != ESP_OK) return true;
    uint32_t frame_len = ((uint32_t)len_bytes[0] << 24) | ((uint32_t)len_bytes[1] << 16) |
                          ((uint32_t)len_bytes[2] << 8) | len_bytes[3];
    if (frame_len == 0 || frame_len > UART_CAM_MAX_FRAME) {
        ESP_LOGW(TAG, "implausible frame length %lu, resyncing", (unsigned long)frame_len);
        return true;
    }

    uint8_t *frame_buf = malloc(frame_len);
    if (!frame_buf) {
        ESP_LOGW(TAG, "out of memory for %lu byte frame", (unsigned long)frame_len);
        return true;
    }

    if (read_exact(frame_buf, frame_len, pdMS_TO_TICKS(2000)) != ESP_OK) {
        free(frame_buf);
        return true;
    }

    uint8_t end_marker[2];
    if (read_exact(end_marker, 2, pdMS_TO_TICKS(200)) != ESP_OK ||
        end_marker[0] != 0xFF || end_marker[1] != 0xD9) {
        ESP_LOGW(TAG, "frame end marker mismatch, discarding frame");
        free(frame_buf);
        return true;
    }

    cam_frame_t item = { .buf = frame_buf, .len = frame_len };
    if (xQueueSend(s_frame_queue, &item, 0) != pdTRUE) {
        cam_frame_t old;
        if (xQueueReceive(s_frame_queue, &old, 0) == pdTRUE) free(old.buf);
        xQueueSend(s_frame_queue, &item, 0);
    }

    state_lock();
    g_state.cam_online = true;
    state_unlock();
    return true;
}

static void uart_cam_task(void *arg)
{
    TickType_t boot_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    bool got_ip = false;
    state_lock();
    got_ip = g_state.cam_ip[0] != '\0';
    state_unlock();

    while (!got_ip && xTaskGetTickCount() < boot_deadline) {
        uint8_t marker;
        if (read_exact(&marker, 1, pdMS_TO_TICKS(500)) == ESP_OK && marker == '{') {
            handle_json_line(marker);
        }
        state_lock();
        got_ip = g_state.cam_ip[0] != '\0';
        state_unlock();
    }
    if (!got_ip) {
        ESP_LOGW(TAG, "no camera IP announcement within 30s; will keep listening");
    }

    for (;;) {
        try_read_frame();
    }
}

void uart_cam_task_start(UBaseType_t priority, BaseType_t core_id)
{
    xTaskCreatePinnedToCore(uart_cam_task, "uart_cam_task", 4096, NULL, priority, NULL, core_id);
}

esp_err_t uart_cam_get_frame(uint8_t **buf, size_t *len, uint32_t timeout_ms)
{
    cam_frame_t item;
    if (xQueueReceive(s_frame_queue, &item, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *buf = item.buf;
    *len = item.len;
    return ESP_OK;
}
