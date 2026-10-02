#pragma once

#include "esp_err.h"

/* AI-Thinker boards have an onboard white LED on GPIO4, wired as a camera
 * flash. Used here purely to let someone trigger it remotely to locate
 * the board — not an actual photography flash sync. */
#define FLASH_LED_GPIO 4
#define FLASH_LED_ON_MS 10000

esp_err_t flash_led_init(void);

/* Turns the LED on for FLASH_LED_ON_MS, then off automatically. Safe to
 * call again while already on — it just extends the window rather than
 * stacking timers. */
void flash_led_trigger(void);
