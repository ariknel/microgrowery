#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "schedule.h"
#include "nvs_config.h"
#include "led_control.h"
#include "wifi_manager.h"
#include "state.h"

static const char *TAG = "schedule";

#define RAMP_MINUTES 5
#define TICK_INTERVAL_MS 60000

/* Anything earlier is the unset 1970 clock, not real time. */
#define CLOCK_VALID_EPOCH 1600000000
#define CLOCK_SAVE_EVERY_TICKS 10

static esp_timer_handle_t s_timer;
/* Starts one short so the first tick with a valid clock saves it right away. */
static int s_ticks_since_clock_save = CLOCK_SAVE_EVERY_TICKS - 1;

/* There's no RTC battery, so a power cut (which a PD Hard Reset is on this
 * board) loses the time; a soft reset keeps it. If WiFi can't sync after a
 * power cut, resume from the last saved time instead of 1970: the schedule
 * keeps running on the built-in clock, behind by however long the power was
 * off, and SNTP corrects it at the next sync. */
static void restore_clock(void)
{
    if (time(NULL) >= CLOCK_VALID_EPOCH) return;

    uint32_t saved = 0;
    nvs_config_get_u32("clock_ts", &saved, 0);
    if (saved < CLOCK_VALID_EPOCH) {
        ESP_LOGW(TAG, "no saved time yet, schedule waits for the first time sync");
        return;
    }
    struct timeval tv = { .tv_sec = saved, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGW(TAG, "clock not synced yet, resuming from last saved time");
}

static void save_clock(void)
{
    time_t now = time(NULL);
    if (now < CLOCK_VALID_EPOCH) return;
    if (++s_ticks_since_clock_save < CLOCK_SAVE_EVERY_TICKS) return;
    s_ticks_since_clock_save = 0;
    nvs_config_set_u32("clock_ts", (uint32_t)now);
}

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
    save_clock();
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
        led_set_output(target);
        return;
    }

    if (time(NULL) < CLOCK_VALID_EPOCH) {
        /* Never synced and nothing saved: only possible before the very
         * first time sync, which follows WiFi provisioning within seconds. */
        state_lock();
        g_state.lights_on = false;
        g_state.minutes_until_transition = 0;
        state_unlock();
        led_set_output(0);
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

    /* Ramp steps go to the output only, never to the setpoint: writing them
     * back as the brightness made each step compound on the last, and the
     * sunset ramp ended by storing 0 so the lights never came back on.
     * Re-applied every tick, so the output always matches the schedule. */
    uint8_t output;
    if (lights_on && since_on < RAMP_MINUTES) {
        output = (uint8_t)(((since_on + 1) * (uint32_t)target) / RAMP_MINUTES);
    } else if (!lights_on && since_off < RAMP_MINUTES) {
        output = (uint8_t)(target - (((since_off + 1) * (uint32_t)target) / RAMP_MINUTES));
    } else {
        output = lights_on ? target : 0;
    }

    state_lock();
    g_state.lights_on = lights_on;
    g_state.minutes_until_transition = until;
    state_unlock();

    led_set_output(output);
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

    /* Timezone at boot, not only once WiFi connects, so a restored clock is
     * read as local time too. */
    setenv("TZ", WIFI_TZ_STRING, 1);
    tzset();
    restore_clock();

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
