#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define UART_CAM_PORT      UART_NUM_1
#define UART_CAM_RX_GPIO   17
#define UART_CAM_TX_GPIO   18
#define UART_CAM_BAUD      921600
#define UART_CAM_RX_BUF    4096

#define UART_CAM_MAX_FRAME (128 * 1024)
#define UART_CAM_FRAME_QUEUE_DEPTH 2

esp_err_t uart_cam_init(void);
void uart_cam_task_start(UBaseType_t priority, BaseType_t core_id);

esp_err_t uart_cam_request_stream_start(void);
esp_err_t uart_cam_request_stream_stop(void);
esp_err_t uart_cam_request_snapshot(void);

/* Triggers the camera board's onboard LED for a few seconds — purely to
 * help locate the physical board, not a photography flash. */
esp_err_t uart_cam_request_flash(void);

/* On ESP_OK, *buf is heap memory the caller owns and must free(). */
esp_err_t uart_cam_get_frame(uint8_t **buf, size_t *len, uint32_t timeout_ms);
