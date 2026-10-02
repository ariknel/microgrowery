#include "driver/gpio.h"
#include "esp_timer.h"
#include "flash_led.h"

static esp_timer_handle_t s_off_timer;

static void off_cb(void *arg)
{
    gpio_set_level(FLASH_LED_GPIO, 0);
}

esp_err_t flash_led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << FLASH_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    err = gpio_set_level(FLASH_LED_GPIO, 0);
    if (err != ESP_OK) return err;

    const esp_timer_create_args_t timer_args = {
        .callback = off_cb,
        .name = "flash_led_off",
    };
    return esp_timer_create(&timer_args, &s_off_timer);
}

void flash_led_trigger(void)
{
    gpio_set_level(FLASH_LED_GPIO, 1);
    if (esp_timer_is_active(s_off_timer)) {
        esp_timer_stop(s_off_timer);
    }
    esp_timer_start_once(s_off_timer, (uint64_t)FLASH_LED_ON_MS * 1000);
}
