#include "esp_log.h"
#include "driver/ledc.h"
#include "led_control.h"
#include "nvs_config.h"
#include "state.h"

static const char *TAG = "led_control";

#define LEDC_TIMER_RES   LEDC_TIMER_10_BIT
#define LEDC_FREQ_HZ     25000
#define LEDC_DUTY_MAX    1023

#define LEDC_CH_LED   LEDC_CHANNEL_0
#define LEDC_CH_FAN1  LEDC_CHANNEL_1
#define LEDC_CH_FAN2  LEDC_CHANNEL_2

static uint32_t percent_to_duty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    return (uint32_t)(((uint32_t)percent * LEDC_DUTY_MAX) / 100);
}

static esp_err_t set_channel_duty(ledc_channel_t channel, uint8_t percent)
{
    uint32_t duty = percent_to_duty(percent);
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

esp_err_t led_control_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channels[3] = {
        {
            .gpio_num = LED_GPIO,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CH_LED,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        },
        {
            .gpio_num = FAN1_GPIO,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CH_FAN1,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        },
        {
            .gpio_num = FAN2_GPIO,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CH_FAN2,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        },
    };
    for (int i = 0; i < 3; i++) {
        err = ledc_channel_config(&channels[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    uint8_t led_pct = 70, fan1_pct = 50, fan2_pct = 60;
    nvs_config_get_u8("led_brightness", &led_pct, 70);
    nvs_config_get_u8("fan1_speed", &fan1_pct, 50);
    nvs_config_get_u8("fan2_speed", &fan2_pct, 60);

    led_set_brightness(led_pct);
    fan_set_speed(1, fan1_pct);
    fan_set_speed(2, fan2_pct);

    return ESP_OK;
}

float led_watts_for(uint8_t percent)
{
    if (percent > 100) percent = 100;
    return ((float)percent / 100.0f) * LED_PANEL_MAX_WATTS;
}

esp_err_t led_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    esp_err_t err = set_channel_duty(LEDC_CH_LED, percent);
    if (err != ESP_OK) return err;

    state_lock();
    g_state.led_brightness = percent;
    state_unlock();

    return nvs_config_set_u8("led_brightness", percent);
}

esp_err_t fan_set_speed(uint8_t fan_id, uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledc_channel_t channel = (fan_id == 1) ? LEDC_CH_FAN1 : LEDC_CH_FAN2;
    esp_err_t err = set_channel_duty(channel, percent);
    if (err != ESP_OK) return err;

    state_lock();
    if (fan_id == 1) g_state.fan1_speed = percent;
    else g_state.fan2_speed = percent;
    state_unlock();

    return nvs_config_set_u8(fan_id == 1 ? "fan1_speed" : "fan2_speed", percent);
}
