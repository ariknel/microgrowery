#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Connects in station mode using credentials the hub sent over UART (see
 * uart_link_wait_for_wifi_creds — this board has no WiFi config of its
 * own). Blocks until the first successful connection, writing the
 * dotted-quad IP into ip_out; keeps itself reconnected afterward via its
 * own event handler. */
esp_err_t wifi_connect_blocking(const char *ssid, const char *password, char *ip_out, size_t ip_out_len);
