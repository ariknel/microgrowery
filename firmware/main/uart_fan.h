#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/* Receives temperature/fan-duty telemetry from the independent Arduino
 * Nano fan controller (see ../../fan_controller/) over UART0 — the hub's
 * J14 debug header, otherwise unused (console goes out over native
 * USB-Serial-JTAG instead, see sdkconfig). Send-only from the Nano's side:
 * the hub never transmits anything back, so only UART0's RX line matters
 * here. Cooling itself doesn't depend on any of this — the Nano runs its
 * own thermal loop regardless of whether the hub is listening. */

#define UART_FAN_PORT UART_NUM_0
#define UART_FAN_BAUD 9600

esp_err_t uart_fan_init(void);
void uart_fan_task_start(UBaseType_t priority, BaseType_t core_id);
