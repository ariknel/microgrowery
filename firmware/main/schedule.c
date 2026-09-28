#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "schedule.h"
#include "nvs_config.h"
#include "led_control.h"
#include "state.h"

static const char *TAG = "schedule";

#define RAMP_MINUTES 5
#define TICK_INTERVAL_MS 60000

typedef enum {
    RAMP_PHASE_OFF = 0,
    RAMP_PHASE_RISING,
    RAMP_PHASE_ON,
    RAMP_PHASE_FALLING,
} ramp_phase_t;

static esp_timer_handle_t s_timer;
static ramp_phase_t s_phase = RAMP_PHASE_OFF;

static uint16_t minutes_since_midnight(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return (uint16_t)(tm_now.tm_hour * 60 + tm_now.tm_min);
}

static bool is_lights_on(uint16_t now_min, uint16_t on_min, uint16_t off_min)
{
    if (on_min == off_min) return true;
    if (on_min < off_min) return now_min >= on_min && now_min < off_min;
    return now_min >= on_min || now_min < off_min;
}

/* Minutes elapsed since `edge_min`, wrapping across midnight, capped so
 * callers can cheaply test "are we inside the N-minute ramp window". */
static int minutes_since(uint16_t edge_min, uint16_t now_min)
{
    int diff = (int)now_min - (int)edge_min;
    if (diff < 0) diff += 1440;
    return diff;
}

static void maybe_seed_first_boot(void)
{
    uint32_t first_boot = 0;
    nvs_config_get_u32("first_boot_ts", &first_boot, 0);
    if (first_boot != 0) {
        state_lock();
        g_state.first_boot_ts = first_boot;
        state_unlock();
        return;
    }

    time_t now = time(NULL);
    if (now < 1600000000) {
        return; /* clock not synced yet, try again next tick */
    }
    nvs_config_set_u32("first_boot_ts", (uint32_t)now);
    state_lock();
    g_state.first_boot_ts = (uint32_t)now;
    state_unlock();
    ESP_LOGI(TAG, "first boot timestamp recorded: %lu", (unsigned long)now);
}

uint32_t schedule_grow_day(void)
{
    state_lock();
    uint32_t first_boot = g_state.first_boot_ts;
    state_unlock();

    if (first_boot == 0) return 0;
    time_t now = time(NULL);
    if (now < (time_t)first_boot) return 0;
    return (uint32_t)((now - (time_t)first_boot) / 86400);
}

static void schedule_tick(void *arg)
{
    (void)arg;
    maybe_seed_first_boot();

    uint16_t on_min, off_min;
    bool enabled;
    uint8_t target;
    state_lock();
    on_min = g_state.sched_on_min;
    off_min = g_state.sched_off_min;
    enabled = g_state.sched_enabled;
    target = g_state.led_brightness;
    state_unlock();

    if (!enabled) {
        state_lock();
        g_state.lights_on = true;
        g_state.minutes_until_transition = 0;
        state_unlock();
        s_phase = RAMP_PHASE_OFF; /* re-armed so re-enabling starts clean */
        return;
    }

    uint16_t now_min = minutes_since_midnight();
    bool lights_on = is_lights_on(now_min, on_min, off_min);
    int since_on = minutes_since(on_min, now_min);
    int since_off = minutes_since(off_min, now_min);

    uint16_t until;
    if (lights_on) {
        until = (uint16_t)minutes_since(now_min, off_min);
    } else {
        until = (uint16_t)minutes_since(now_min, on_min);
    }

    if (lights_on && since_on < RAMP_MINUTES) {
        uint8_t step = (uint8_t)(((since_on + 1) * (uint32_t)target) / RAMP_MINUTES);
        led_set_brightness(step);
        s_phase = RAMP_PHASE_RISING;
    } else if (!lights_on && since_off < RAMP_MINUTES) {
        uint8_t step = (uint8_t)(target - (((since_off + 1) * (uint32_t)target) / RAMP_MINUTES));
        led_set_brightness(step);
        s_phase = RAMP_PHASE_FALLING;
    } else if (lights_on && s_phase != RAMP_PHASE_ON) {
        led_set_brightness(target);
        s_phase = RAMP_PHASE_ON;
    } else if (!lights_on && s_phase != RAMP_PHASE_OFF) {
        led_set_brightness(0);
        s_phase = RAMP_PHASE_OFF;
    }

    state_lock();
    g_state.lights_on = lights_on;
    g_state.minutes_until_transition = until;
    state_unlock();
}

esp_err_t schedule_init(void)
{
    uint16_t on_min = 360, off_min = 1320;
    uint8_t enabled = 1;
    nvs_config_get_u16("sched_on", &on_min, 360);
    nvs_config_get_u16("sched_off", &off_min, 1320);
    nvs_config_get_u8("sched_enabled", &enabled, 1);

    state_lock();
    g_state.sched_on_min = on_min;
    g_state.sched_off_min = off_min;
    g_state.sched_enabled = enabled != 0;
    state_unlock();

    maybe_seed_first_boot();

    const esp_timer_create_args_t timer_args = {
        .callback = schedule_tick,
        .name = "schedule_tick",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) return err;

    schedule_tick(NULL); /* establish correct state immediately, don't wait 60s */
    return esp_timer_start_periodic(s_timer, (uint64_t)TICK_INTERVAL_MS * 1000);
}

esp_err_t schedule_set(uint16_t on_min, uint16_t off_min, bool enabled)
{
    if (on_min > 1439 || off_min > 1439) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nvs_config_set_u16("sched_on", on_min);
    if (err != ESP_OK) return err;
    err = nvs_config_set_u16("sched_off", off_min);
    if (err != ESP_OK) return err;
    err = nvs_config_set_u8("sched_enabled", enabled ? 1 : 0);
    if (err != ESP_OK) return err;

    state_lock();
    g_state.sched_on_min = on_min;
    g_state.sched_off_min = off_min;
    g_state.sched_enabled = enabled;
    state_unlock();

    schedule_tick(NULL); /* reflect the new schedule right away */
    return ESP_OK;
}
