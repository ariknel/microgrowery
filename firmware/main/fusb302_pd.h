#pragma once

#include <stdint.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PD_REQUEST_VOLTAGE_MV     20000
/* Ask for whatever the PDO offers, up to 5A: LEDs (65W) plus the rest of the
 * board need more than 3A at 20V, and drawing past the requested current
 * gets VBUS cut. LED output is capped to the contract in led_control. */
#define PD_REQUEST_MAX_CURRENT_MA 5000

/* Wait this long after attach for an unsolicited Source_Capabilities before
 * asking with Get_Source_Cap (spec tTypeCSinkWaitCap is 310-620ms). */
#define PD_GET_CAP_DELAY_MS       1000
#define PD_GET_CAP_INTERVAL_MS    2000
#define PD_WAIT_CAPS_MS           12000
#define PD_RETRY_INTERVAL_MS      5000

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
#define PD_CTRL_DR_SWAP        0x09
#define PD_CTRL_PR_SWAP        0x0A
#define PD_CTRL_VCONN_SWAP     0x0B
#define PD_CTRL_WAIT           0x0C
#define PD_CTRL_SOFT_RESET     0x0D

/* PD data message types (NDO > 0) */
#define PD_DATA_SOURCE_CAP     0x01
#define PD_DATA_REQUEST        0x02
#define PD_DATA_BIST           0x03
#define PD_DATA_SINK_CAP       0x04
#define PD_DATA_VENDOR_DEFINED 0x0F

/* Spawns the PD negotiation task. fusb302_init() is called from within the
 * task so the FUSB302 and its GPIO interrupt are only touched from pd_task's
 * context. */
void pd_task_start(i2c_port_t i2c_port, UBaseType_t priority, BaseType_t core_id);
