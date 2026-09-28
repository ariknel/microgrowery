#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CAM_IP_MAXLEN 16
#define FW_VERSION_MAXLEN 16

typedef struct {
    /* sensors (AHT20) */
    float temp_c;
    float humidity_pct;
    float temp_min;
    float temp_max;
    float humidity_min;
    float humidity_max;
    bool sensor_valid;

    /* actuators */
    uint8_t led_brightness;   /* 0-100 */
    uint8_t fan1_speed;       /* 0-100, intake */
    uint8_t fan2_speed;       /* 0-100, exhaust */

    /* USB PD (FUSB302) */
    bool pd_negotiated;
    uint32_t pd_voltage_mv;
    uint32_t pd_current_ma;

    /* wifi */
    bool wifi_connected;
    int8_t wifi_rssi;

    /* camera (ESP32-CAM over UART) */
    char cam_ip[CAM_IP_MAXLEN];
    bool cam_online;

    /* schedule */
    uint16_t sched_on_min;
    uint16_t sched_off_min;
    bool sched_enabled;
    bool lights_on;
    uint16_t minutes_until_transition;

    /* misc */
    uint32_t first_boot_ts;
    char firmware_version[FW_VERSION_MAXLEN];
} growbox_state_t;

extern growbox_state_t g_state;

void state_init(void);

/* Every read/write of g_state must happen between state_lock()/state_unlock(). */
void state_lock(void);
void state_unlock(void);
