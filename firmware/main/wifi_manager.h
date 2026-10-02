#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_AP_SSID "GrowBox-Setup"
#define WIFI_AP_PASS "growbox123"
#define WIFI_MDNS_HOSTNAME "growbox"
#define WIFI_RETRY_INTERVAL_MS 30000

/* If stored credentials exist but STA hasn't connected within this long,
 * bring up WIFI_AP_SSID alongside the still-ongoing STA retries (APSTA
 * mode) so the device stays reachable even if the stored WiFi is down or
 * wrong — reusing the same dashboard httpd already running, not a
 * separate captive portal. Stays up indefinitely once triggered. */
#define WIFI_FALLBACK_AP_DELAY_MS 15000

/* POSIX TZ string, default Europe/Brussels (CET/CEST with EU DST rules). */
#define WIFI_TZ_STRING "CET-1CEST,M3.5.0,M10.5.0/3"

/* True if NVS has no stored SSID yet — caller should run the captive
 * portal instead of the normal boot sequence. */
bool wifi_manager_needs_provisioning(void);

/* Blocks forever serving the SoftAP captive portal. Restarts the device
 * once credentials are submitted; never returns otherwise. */
void wifi_manager_run_provisioning(void);

/* Starts station mode using stored credentials, with a background task
 * that retries every WIFI_RETRY_INTERVAL_MS on failure and re-syncs SNTP
 * / mDNS / g_state on each successful (re)connect. */
esp_err_t wifi_manager_start_station(void);
