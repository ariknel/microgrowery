#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "uart_cam.h"
#include "state.h"

static const char *TAG = "uart_cam";

typedef struct {
    uint8_t *buf;
    size_t len;
} cam_frame_t;

static QueueHandle_t s_frame_queue;

/* Last successfully received frame, kept around independently of the
 * (single-consumer) frame queue so a frozen view has something to serve
 * even while no one's actively pulling from the queue. */
static SemaphoreHandle_t s_cache_mutex;
static uint8_t *s_cached_frame = NULL;
static size_t s_cached_frame_len = 0;

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
    s_cache_mutex = xSemaphoreCreateMutex();
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

    cJSON *ready = cJSON_GetObjectItemCaseSensitive(root, "ready");
    if (cJSON_IsTrue(ready)) {
        state_lock();
        g_state.cam_online = true;
        state_unlock();
        ESP_LOGI(TAG, "camera board ready");
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

    uint8_t *cache_copy = malloc(frame_len);
    if (cache_copy) {
        memcpy(cache_copy, frame_buf, frame_len);
        if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            free(s_cached_frame);
            s_cached_frame = cache_copy;
            s_cached_frame_len = frame_len;
            xSemaphoreGive(s_cache_mutex);
        } else {
            free(cache_copy);
        }
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
    bool ready = false;
    state_lock();
    ready = g_state.cam_online;
    state_unlock();

    while (!ready && xTaskGetTickCount() < boot_deadline) {
        uint8_t marker;
        if (read_exact(&marker, 1, pdMS_TO_TICKS(500)) == ESP_OK && marker == '{') {
            handle_json_line(marker);
        }
        state_lock();
        ready = g_state.cam_online;
        state_unlock();
    }
    if (!ready) {
        ESP_LOGW(TAG, "no camera ready announcement within 30s; will keep listening");
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

esp_err_t uart_cam_get_cached_frame(uint8_t **buf, size_t *len)
{
    esp_err_t result = ESP_ERR_NOT_FOUND;
    if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_cached_frame && s_cached_frame_len > 0) {
        uint8_t *copy = malloc(s_cached_frame_len);
        if (copy) {
            memcpy(copy, s_cached_frame, s_cached_frame_len);
            *buf = copy;
            *len = s_cached_frame_len;
            result = ESP_OK;
        } else {
            result = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_cache_mutex);
    return result;
}
