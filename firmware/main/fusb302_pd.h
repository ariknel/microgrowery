#pragma once

#include <stdint.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PD_REQUEST_VOLTAGE_MV   20000
#define PD_REQUEST_MAX_CURRENT_MA 3250
#define PD_RETRY_INTERVAL_MS    30000

#define PD_EVENT_QUEUE_DEPTH 8

/* PD control message types (NDO == 0) */
#define PD_CTRL_GOODCRC        0x01
#define PD_CTRL_GOTOMIN        0x02
#define PD_CTRL_ACCEPT         0x03
#define PD_CTRL_REJECT         0x04
#define PD_CTRL_PING           0x05
#define PD_CTRL_PS_RDY         0x06
#define PD_CTRL_GET_SRC_CAP    0x07
#define PD_CTRL_GET_SNK_CAP    0x08
#define PD_CTRL_WAIT           0x0C
#define PD_CTRL_SOFT_RESET     0x0D

/* PD data message types (NDO > 0) */
#define PD_DATA_SOURCE_CAP     0x01
#define PD_DATA_REQUEST        0x02
#define PD_DATA_BIST           0x03
#define PD_DATA_SINK_CAP       0x04

/* Spawns the PD negotiation task. i2c_port and event_queue must already
 * be initialized; fusb302_init() is called from within the task so the
 * FUSB302 and its GPIO interrupt are only touched from pd_task's context. */
void pd_task_start(i2c_port_t i2c_port, UBaseType_t priority, BaseType_t core_id);
