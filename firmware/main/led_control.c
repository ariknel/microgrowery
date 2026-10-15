#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
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

/* The LED boards and the fans' 12V buck run straight off USB-C VBUS. Until a
 * 20V PD contract exists VBUS is 5V and the source may only allow 0.5-1.5A,
 * so nothing is driven (s_power_mw == 0); after that, LED duty is capped to
 * what the contract can feed. Setpoints live in g_state, the schedule's
 * requested LED output in s_led_request. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_led_request;
static uint32_t s_power_mw;

static uint32_t percent_to_duty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    return (uint32_t)(((uint32_t)percent * LEDC_DUTY_MAX) / 100);
}

static void set_channel_duty(ledc_channel_t channel, uint8_t percent)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, percent_to_duty(percent));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static uint8_t led_cap_for(uint32_t power_mw)
{
    if (power_mw <= NON_LED_BUDGET_MW) return 0;
    uint32_t pct = (power_mw - NON_LED_BUDGET_MW) / (uint32_t)(LED_PANEL_MAX_WATTS * 10.0f);
    return pct > 100 ? 100 : (uint8_t)pct;
}

static void apply_outputs(void)
{
    taskENTER_CRITICAL(&s_lock);
    uint8_t led = s_led_request;
    uint32_t power_mw = s_power_mw;
    taskEXIT_CRITICAL(&s_lock);

    uint8_t cap = led_cap_for(power_mw);
    if (led > cap) led = cap;

    uint8_t fan1 = 0, fan2 = 0;
    if (power_mw > 0) {
        state_lock();
        fan1 = g_state.fan1_speed;
        fan2 = g_state.fan2_speed;
        state_unlock();
    }

    set_channel_duty(LEDC_CH_LED, led);
    set_channel_duty(LEDC_CH_FAN1, fan1);
    set_channel_duty(LEDC_CH_FAN2, fan2);
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

    const int gpios[3] = { LED_GPIO, FAN1_GPIO, FAN2_GPIO };
    const ledc_channel_t chans[3] = { LEDC_CH_LED, LEDC_CH_FAN1, LEDC_CH_FAN2 };
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ch = {
            .gpio_num = gpios[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = chans[i],
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    uint8_t led_pct = 70, fan1_pct = 50, fan2_pct = 60;
    nvs_config_get_u8("led_brightness", &led_pct, 70);
    nvs_config_get_u8("fan1_speed", &fan1_pct, 50);
    nvs_config_get_u8("fan2_speed", &fan2_pct, 60);
    if (led_pct > 100) led_pct = 100;
    if (fan1_pct > 100) fan1_pct = 100;
    if (fan2_pct > 100) fan2_pct = 100;

    state_lock();
    g_state.led_brightness = led_pct;
    g_state.fan1_speed = fan1_pct;
    g_state.fan2_speed = fan2_pct;
    state_unlock();

    /* All outputs stay at 0 until the schedule asks for light and a 20V
     * contract exists. */
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

    state_lock();
    g_state.led_brightness = percent;
    bool lights_on = g_state.lights_on;
    state_unlock();

    if (lights_on) led_set_output(percent);
    return nvs_config_set_u8("led_brightness", percent);
}

esp_err_t fan_set_speed(uint8_t fan_id, uint8_t percent)
{
    if (percent > 100) percent = 100;

    state_lock();
    if (fan_id == 1) g_state.fan1_speed = percent;
    else g_state.fan2_speed = percent;
    state_unlock();

    apply_outputs();
    return nvs_config_set_u8(fan_id == 1 ? "fan1_speed" : "fan2_speed", percent);
}

void led_set_output(uint8_t percent)
{
    if (percent > 100) percent = 100;
    taskENTER_CRITICAL(&s_lock);
    s_led_request = percent;
    taskEXIT_CRITICAL(&s_lock);
    apply_outputs();
}

void led_set_power_budget(uint32_t contract_mw)
{
    taskENTER_CRITICAL(&s_lock);
    s_power_mw = contract_mw;
    taskEXIT_CRITICAL(&s_lock);
    apply_outputs();
    ESP_LOGI(TAG, "power budget %lu mW, LED cap %d%%", (unsigned long)contract_mw, led_cap_for(contract_mw));
}
