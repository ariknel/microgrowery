#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Connects in station mode using CONFIG_GROWBOX_WIFI_SSID/PASSWORD (set via
 * `idf.py menuconfig` or sdkconfig.defaults — same network as the hub).
 * Blocks until the first successful connection, writing the dotted-quad IP
 * into ip_out; keeps itself reconnected afterward via its own event
 * handler. */
esp_err_t wifi_connect_blocking(char *ip_out, size_t ip_out_len);
