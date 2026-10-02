#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_server.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "wifi_manager.h"
#include "nvs_config.h"
#include "state.h"

static const char *TAG = "wifi_manager";

#define WIFI_SSID_MAXLEN 33
#define WIFI_PASS_MAXLEN 65

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

/* ---------------- station mode ---------------- */

static void start_sntp_and_tz(void)
{
    setenv("TZ", WIFI_TZ_STRING, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    ESP_LOGI(TAG, "SNTP sync started (TZ=%s)", WIFI_TZ_STRING);
}

static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(WIFI_MDNS_HOSTNAME);
    mdns_instance_name_set("Microgrowery Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS hostname: %s.local", WIFI_MDNS_HOSTNAME);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, will retry");
        state_lock();
        g_state.wifi_connected = false;
        state_unlock();
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        state_lock();
        g_state.wifi_connected = true;
        state_unlock();

        start_mdns();
        start_sntp_and_tz();
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* Brings up the same setup AP alongside STA (APSTA mode) without tearing
 * down the ongoing STA retry loop — lets the device stay reachable (over
 * the existing dashboard httpd, already running regardless of STA state)
 * even if the stored WiFi credentials are wrong or that network is down.
 * There's no dedicated reconfiguration form wired up in the dashboard yet
 * — POST /api/config/wifi exists and is reachable over this AP, but you'd
 * need to call it directly (curl, etc.) rather than through a UI button. */
static void start_fallback_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to switch to APSTA: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure fallback AP: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGW(TAG, "STA still not connected after %ds, fallback AP '%s' up at "
                  "192.168.4.1 (STA retries continue in the background)",
             WIFI_FALLBACK_AP_DELAY_MS / 1000, WIFI_AP_SSID);
}

static void wifi_task(void *arg)
{
    TickType_t sta_attempt_start = xTaskGetTickCount();
    bool fallback_ap_started = false;
    bool was_connected = false;

    for (;;) {
        EventBits_t bits = xEventGroupGetBits(s_wifi_events);
        if (bits & WIFI_CONNECTED_BIT) {
            was_connected = true;
            wifi_ap_record_t info;
            if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
                state_lock();
                g_state.wifi_rssi = info.rssi;
                state_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else {
            /* Restart the fallback-AP countdown from the moment THIS
             * disconnection began, not from when the task first started —
             * otherwise a drop after days of being happily connected would
             * fire the AP almost instantly (elapsed-since-boot already far
             * exceeds the delay) instead of giving this reconnect attempt
             * its own few-second grace period. */
            if (was_connected) {
                sta_attempt_start = xTaskGetTickCount();
                was_connected = false;
            }

            if (!fallback_ap_started &&
                (xTaskGetTickCount() - sta_attempt_start) > pdMS_TO_TICKS(WIFI_FALLBACK_AP_DELAY_MS)) {
                start_fallback_ap();
                fallback_ap_started = true;
            }
            ESP_LOGI(TAG, "not connected, retrying in %d ms", WIFI_RETRY_INTERVAL_MS);
            esp_wifi_connect();
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));
        }
    }
}

bool wifi_manager_needs_provisioning(void)
{
    char ssid[WIFI_SSID_MAXLEN] = { 0 };
    nvs_config_get_str("wifi_ssid", ssid, sizeof(ssid), "");
    return ssid[0] == '\0';
}

esp_err_t wifi_manager_start_station(void)
{
    char ssid[WIFI_SSID_MAXLEN] = { 0 };
    char pass[WIFI_PASS_MAXLEN] = { 0 };
    nvs_config_get_str("wifi_ssid", ssid, sizeof(ssid), "");
    nvs_config_get_str("wifi_pass", pass, sizeof(pass), "");

    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreatePinnedToCore(wifi_task, "wifi_task", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    return ESP_OK;
}

/* ---------------- SoftAP provisioning ---------------- */

static const char PROVISION_FORM[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>GrowBox Setup</title>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;display:flex;"
    "align-items:center;justify-content:center;min-height:100vh;margin:0;}"
    "form{background:#1c1c1c;padding:24px;border-radius:12px;width:280px;}"
    "h1{font-size:1.1rem;margin:0 0 16px;}"
    "input{width:100%;box-sizing:border-box;padding:10px;margin-bottom:12px;"
    "border-radius:6px;border:1px solid #444;background:#0a0a0a;color:#eee;}"
    "button{width:100%;padding:10px;border-radius:6px;border:none;"
    "background:#4caf50;color:#fff;font-weight:bold;}</style></head><body>"
    "<form method=\"POST\" action=\"/save\">"
    "<h1>Connect GrowBox to WiFi</h1>"
    "<input name=\"ssid\" placeholder=\"WiFi network name\" required>"
    "<input name=\"pass\" type=\"password\" placeholder=\"WiFi password\">"
    "<button type=\"submit\">Save &amp; Restart</button>"
    "</form></body></html>";

static const char PROVISION_SAVED[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"></head>"
    "<body style=\"font-family:sans-serif;background:#111;color:#eee;\">"
    "<p>Saved. Restarting&hellip;</p></body></html>";

static esp_err_t provision_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PROVISION_FORM, HTTPD_RESP_USE_STRLEN);
}

static void url_decode(char *dst, const char *src)
{
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = (src[1] <= '9') ? src[1] - '0' : (src[1] | 0x20) - 'a' + 10;
            int lo = (src[2] <= '9') ? src[2] - '0' : (src[2] | 0x20) - 'a' + 10;
            *dst++ = (char)((hi << 4) | lo);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void restart_after_delay(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static esp_err_t provision_save_handler(httpd_req_t *req)
{
    char body[256] = { 0 };
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
    }
    body[len] = '\0';

    char ssid_enc[WIFI_SSID_MAXLEN * 3] = { 0 };
    char pass_enc[WIFI_PASS_MAXLEN * 3] = { 0 };
    httpd_query_key_value(body, "ssid", ssid_enc, sizeof(ssid_enc));
    httpd_query_key_value(body, "pass", pass_enc, sizeof(pass_enc));

    char ssid[WIFI_SSID_MAXLEN] = { 0 };
    char pass[WIFI_PASS_MAXLEN] = { 0 };
    url_decode(ssid, ssid_enc);
    url_decode(pass, pass_enc);

    if (ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
    }

    nvs_config_set_str("wifi_ssid", ssid);
    nvs_config_set_str("wifi_pass", pass);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PROVISION_SAVED, HTTPD_RESP_USE_STRLEN);

    xTaskCreate(restart_after_delay, "restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

void wifi_manager_run_provisioning(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "provisioning AP '%s' started, portal at 192.168.4.1", WIFI_AP_SSID);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t get_uri = { .uri = "/", .method = HTTP_GET, .handler = provision_get_handler };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = provision_save_handler };
    httpd_register_uri_handler(server, &get_uri);
    httpd_register_uri_handler(server, &save_uri);

    /* This mode never returns; the device restarts once credentials are
     * submitted (see restart_after_delay) and boots normally next time. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
