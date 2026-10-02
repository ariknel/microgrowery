#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera.h"
#include "uart_link.h"
#include "wifi_link.h"

static const char *TAG = "main";

/* ~3fps, matching what the hub/dashboard expect (see BUILD.md in the main
 * project). Streaming only runs while the hub has asked for it. */
#define STREAM_FRAME_INTERVAL_MS 300

static void send_and_release(void)
{
    camera_fb_t *fb = camera_capture();
    if (!fb) {
        ESP_LOGW(TAG, "frame capture failed");
        return;
    }
    uart_link_send_frame(fb);
    camera_release(fb);
}

static void stream_task(void *arg)
{
    for (;;) {
        if (uart_link_streaming_enabled()) {
            send_and_release();
            vTaskDelay(pdMS_TO_TICKS(STREAM_FRAME_INTERVAL_MS));
        } else if (uart_link_consume_snapshot_request()) {
            send_and_release();
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(camera_init());
    ESP_ERROR_CHECK(uart_link_init());

    char ip[16] = { 0 };
    ESP_ERROR_CHECK(wifi_connect_blocking(ip, sizeof(ip)));
    uart_link_send_cam_ip(ip);

    xTaskCreatePinnedToCore(uart_link_command_task, "cam_cmd_task", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(stream_task, "cam_stream_task", 4096, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "camera module ready");
}
