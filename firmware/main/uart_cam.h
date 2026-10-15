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

esp_err_t uart_cam_init(void);
void uart_cam_task_start(UBaseType_t priority, BaseType_t core_id);

/* Reference-counted live streaming: the camera is told to stream while at
 * least one viewer holds it, and to stop when the last one releases. */
void uart_cam_live_acquire(void);
void uart_cam_live_release(void);

esp_err_t uart_cam_request_snapshot(void);

/* Triggers the camera board's onboard LED for a few seconds — purely to
 * help locate the physical board, not a photography flash. */
esp_err_t uart_cam_request_flash(void);

/* Waits up to timeout_ms for a frame newer than *seq, then returns a heap
 * copy (caller must free(*buf)) and updates *seq. Every viewer gets every
 * frame. Start with *seq = 0. */
esp_err_t uart_cam_get_frame_after(uint32_t *seq, uint8_t **buf, size_t *len, uint32_t timeout_ms);

/* Returns a heap copy (caller must free(*buf)) of the most recently
 * received frame — used to serve a frozen view. ESP_ERR_NOT_FOUND if no
 * frame has been received yet this boot. */
esp_err_t uart_cam_get_cached_frame(uint8_t **buf, size_t *len);
