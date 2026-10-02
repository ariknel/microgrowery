#include "esp_log.h"
#include "camera.h"
#include "camera_pins.h"

static const char *TAG = "camera";

#define CAM_XCLK_FREQ_HZ 20000000
#define CAM_JPEG_QUALITY 12 /* lower = better quality, larger frames; 10-15 is a sane range */

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA, /* 320x240, matches the hub's expectation */
        .jpeg_quality = CAM_JPEG_QUALITY,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x — check PSRAM is enabled "
                       "and the pin mapping matches your board", err);
        return err;
    }

    ESP_LOGI(TAG, "camera ready: QVGA JPEG");
    return ESP_OK;
}

camera_fb_t *camera_capture(void)
{
    return esp_camera_fb_get();
}

void camera_release(camera_fb_t *fb)
{
    esp_camera_fb_return(fb);
}
