#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_camera.h"

/* UART1 to the GrowBox hub. GPIO1/GPIO3 (UART0) are already spoken for by
 * the external USB-serial programmer on AI-Thinker boards, so this uses
 * two of the few genuinely free pins instead. Verify against your board's
 * silkscreen if these are in use for something else on your specific
 * clone. */
#define CAM_UART_PORT    UART_NUM_1
#define CAM_UART_TX_GPIO 14
#define CAM_UART_RX_GPIO 15
#define CAM_UART_BAUD    921600

esp_err_t uart_link_init(void);

void uart_link_send_cam_ip(const char *ip);
void uart_link_send_frame(camera_fb_t *fb);

/* Blocks forever reading newline-terminated JSON command lines from the
 * hub ({"cmd":"stream","state":1|0} / {"cmd":"snapshot"}). Intended as a
 * dedicated task entrypoint. */
void uart_link_command_task(void *arg);

bool uart_link_streaming_enabled(void);
bool uart_link_consume_snapshot_request(void);
