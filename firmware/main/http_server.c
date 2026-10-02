#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "http_server.h"
#include "state.h"
#include "auth.h"
#include "nvs_config.h"
#include "led_control.h"
#include "schedule.h"
#include "uart_cam.h"

static const char *TAG = "http_server";

/* dashboard.html is embedded verbatim at build time (see main/CMakeLists.txt
 * EMBED_TXTFILES) — it's the same file served from the browser side of
 * this project, not a placeholder. */
extern const uint8_t dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const uint8_t dashboard_html_end[] asm("_binary_dashboard_html_end");

/* ---------------- helpers ---------------- */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

/* Built manually rather than via HTTPD_401_UNAUTHORIZED — that enum
 * member isn't present in every ESP-IDF v5.x point release, and this
 * works identically across all of them. */
static esp_err_t send_401(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) return ESP_FAIL;
    buf[received] = '\0';
    return ESP_OK;
}

/* Returns true if authorized. On false, an error response has already
 * been sent — the caller should just `return ESP_OK;`. */
static bool require_auth(httpd_req_t *req)
{
    char header[80];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        send_401(req, "missing token");
        return false;
    }
    static const char prefix[] = "Bearer ";
    size_t plen = sizeof(prefix) - 1;
    if (strncmp(header, prefix, plen) != 0 || !auth_check_token(header + plen)) {
        send_401(req, "invalid token");
        return false;
    }
    return true;
}

static void minutes_to_hhmm(uint16_t minutes, char *out)
{
    /* minutes is a full uint16_t to the compiler, so without this it can't
     * prove hours stays 2 digits and flags a possible format truncation —
     * callers already only ever pass 0-1439, this just makes that provable. */
    minutes %= 1440;
    snprintf(out, 6, "%02d:%02d", minutes / 60, minutes % 60);
}

static bool hhmm_to_minutes(const char *hhmm, uint16_t *out)
{
    int h, m;
    if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    *out = (uint16_t)(h * 60 + m);
    return true;
}

/* ---------------- GET / ---------------- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = dashboard_html_end - dashboard_html_start;
    return httpd_resp_send(req, (const char *)dashboard_html_start, len);
}

/* ---------------- GET /api/sensors (public) ---------------- */

static esp_err_t api_sensors_get_handler(httpd_req_t *req)
{
    growbox_state_t snap;
    state_lock();
    snap = g_state;
    state_unlock();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", snap.sensor_valid ? snap.temp_c : 0);
    cJSON_AddNumberToObject(root, "humidity", snap.sensor_valid ? snap.humidity_pct : 0);
    cJSON_AddNumberToObject(root, "temp_min", snap.sensor_valid ? snap.temp_min : 0);
    cJSON_AddNumberToObject(root, "temp_max", snap.sensor_valid ? snap.temp_max : 0);
    cJSON_AddNumberToObject(root, "humidity_min", snap.sensor_valid ? snap.humidity_min : 0);
    cJSON_AddNumberToObject(root, "humidity_max", snap.sensor_valid ? snap.humidity_max : 0);
    return send_json(req, root);
}

/* ---------------- GET /api/camera (public) ---------------- */

static esp_err_t api_camera_get_handler(httpd_req_t *req)
{
    char cam_ip[CAM_IP_MAXLEN];
    bool online;
    state_lock();
    strncpy(cam_ip, g_state.cam_ip, sizeof(cam_ip));
    online = g_state.cam_online;
    state_unlock();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cam_ip", cam_ip);
    cJSON_AddBoolToObject(root, "online", online);
    /* "/stream" is this device's own proxy endpoint, not the cam's raw
     * address — the browser never talks to the ESP32-CAM directly. */
    cJSON_AddStringToObject(root, "stream_url", cam_ip[0] ? "/stream" : "");
    return send_json(req, root);
}

/* ---------------- POST /api/camera/flash (public) ----------------
   Triggers the camera board's onboard LED for a few seconds so it can be
   found in person — not a device-state change worth gating behind auth,
   same reasoning as /api/camera and /api/sensors being public. */

static esp_err_t api_camera_flash_post_handler(httpd_req_t *req)
{
    uart_cam_request_flash();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    return send_json(req, resp);
}

/* ---------------- GET /stream (public, proxied MJPEG) ---------------- */

static esp_err_t stream_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    uart_cam_request_stream_start();

    esp_err_t res = ESP_OK;
    for (;;) {
        uint8_t *buf;
        size_t len;
        if (uart_cam_get_frame(&buf, &len, 5000) != ESP_OK) {
            ESP_LOGW(TAG, "no frame for 5s, stopping stream");
            state_lock();
            g_state.cam_online = false;
            state_unlock();
            break;
        }

        char header[64];
        int hlen = snprintf(header, sizeof(header),
                             "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                             (unsigned)len);
        res = httpd_resp_send_chunk(req, header, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)buf, len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
        free(buf);
        if (res != ESP_OK) break; /* client disconnected */
    }

    uart_cam_request_stream_stop();
    httpd_resp_send_chunk(req, NULL, 0);
    return res;
}

/* ---------------- POST /api/auth (public) ---------------- */

static esp_err_t api_auth_post_handler(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsString(code)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing code");
    }

    char token[AUTH_TOKEN_LEN + 1];
    bool ok = auth_check_code(code->valuestring, token);
    cJSON_Delete(root);

    if (!ok) return send_401(req, "invalid code");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "token", token);
    return send_json(req, resp);
}

/* ---------------- GET /api/status (protected) ---------------- */

static esp_err_t api_status_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    growbox_state_t snap;
    state_lock();
    snap = g_state;
    state_unlock();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", snap.sensor_valid ? snap.temp_c : 0);
    cJSON_AddNumberToObject(root, "humidity", snap.sensor_valid ? snap.humidity_pct : 0);
    cJSON_AddNumberToObject(root, "temp_min", snap.sensor_valid ? snap.temp_min : 0);
    cJSON_AddNumberToObject(root, "temp_max", snap.sensor_valid ? snap.temp_max : 0);
    cJSON_AddNumberToObject(root, "humidity_min", snap.sensor_valid ? snap.humidity_min : 0);
    cJSON_AddNumberToObject(root, "humidity_max", snap.sensor_valid ? snap.humidity_max : 0);
    cJSON_AddNumberToObject(root, "led_brightness", snap.led_brightness);
    cJSON_AddNumberToObject(root, "led_watts", led_watts_for(snap.led_brightness));
    cJSON_AddNumberToObject(root, "fan1_speed", snap.fan1_speed);
    cJSON_AddNumberToObject(root, "fan2_speed", snap.fan2_speed);
    cJSON_AddBoolToObject(root, "pd_negotiated", snap.pd_negotiated);
    cJSON_AddNumberToObject(root, "pd_voltage_mv", snap.pd_voltage_mv);
    cJSON_AddNumberToObject(root, "pd_current_ma", snap.pd_current_ma);
    cJSON_AddNumberToObject(root, "wifi_rssi", snap.wifi_rssi);
    cJSON_AddNumberToObject(root, "uptime_seconds", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "grow_day", schedule_grow_day());
    cJSON_AddBoolToObject(root, "lights_on", snap.lights_on);
    cJSON_AddNumberToObject(root, "minutes_until_transition", snap.minutes_until_transition);
    cJSON_AddStringToObject(root, "cam_ip", snap.cam_ip);
    cJSON_AddBoolToObject(root, "cam_online", snap.cam_online);
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());
    cJSON_AddStringToObject(root, "firmware_version", snap.firmware_version);
    return send_json(req, root);
}

/* ---------------- POST /api/led (protected) ---------------- */

static esp_err_t api_led_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    char body[64];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (!cJSON_IsNumber(b)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing brightness");
    }
    int pct = b->valueint;
    cJSON_Delete(root);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    led_set_brightness((uint8_t)pct);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "watts", led_watts_for((uint8_t)pct));
    return send_json(req, resp);
}

/* ---------------- POST /api/fans (protected) ---------------- */

static esp_err_t api_fans_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    char body[64];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    uint8_t fan1, fan2;
    state_lock();
    fan1 = g_state.fan1_speed;
    fan2 = g_state.fan2_speed;
    state_unlock();

    cJSON *f1 = cJSON_GetObjectItemCaseSensitive(root, "fan1");
    cJSON *f2 = cJSON_GetObjectItemCaseSensitive(root, "fan2");
    if (cJSON_IsNumber(f1)) {
        int v = f1->valueint;
        fan1 = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
    }
    if (cJSON_IsNumber(f2)) {
        int v = f2->valueint;
        fan2 = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
    }
    cJSON_Delete(root);

    fan_set_speed(1, fan1);
    fan_set_speed(2, fan2);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    return send_json(req, resp);
}

/* ---------------- GET /api/schedule (protected) ---------------- */

static esp_err_t api_schedule_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    uint16_t on_min, off_min, until;
    bool enabled, lights_on;
    state_lock();
    on_min = g_state.sched_on_min;
    off_min = g_state.sched_off_min;
    enabled = g_state.sched_enabled;
    lights_on = g_state.lights_on;
    until = g_state.minutes_until_transition;
    state_unlock();

    char on_str[6], off_str[6];
    minutes_to_hhmm(on_min, on_str);
    minutes_to_hhmm(off_min, off_str);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "on", on_str);
    cJSON_AddStringToObject(resp, "off", off_str);
    cJSON_AddBoolToObject(resp, "enabled", enabled);
    cJSON_AddBoolToObject(resp, "lights_on", lights_on);
    cJSON_AddNumberToObject(resp, "minutes_until_transition", until);
    return send_json(req, resp);
}

/* ---------------- POST /api/schedule (protected) ---------------- */

static esp_err_t api_schedule_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    uint16_t on_min, off_min;
    bool enabled;
    state_lock();
    on_min = g_state.sched_on_min;
    off_min = g_state.sched_off_min;
    enabled = g_state.sched_enabled;
    state_unlock();

    cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "off");
    cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");

    if (cJSON_IsString(on) && !hhmm_to_minutes(on->valuestring, &on_min)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad on time");
    }
    if (cJSON_IsString(off) && !hhmm_to_minutes(off->valuestring, &off_min)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad off time");
    }
    if (cJSON_IsBool(en)) enabled = cJSON_IsTrue(en);
    cJSON_Delete(root);

    schedule_set(on_min, off_min, enabled);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    return send_json(req, resp);
}

/* ---------------- POST /api/config/wifi (protected) ---------------- */

static void restart_after_delay_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static esp_err_t api_config_wifi_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    char body[192];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *pass = cJSON_GetObjectItemCaseSensitive(root, "pass");
    if (!cJSON_IsString(ssid)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }

    nvs_config_set_str("wifi_ssid", ssid->valuestring);
    nvs_config_set_str("wifi_pass", cJSON_IsString(pass) ? pass->valuestring : "");
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "restarting", true);
    esp_err_t err = send_json(req, resp);

    xTaskCreate(restart_after_delay_task, "restart", 2048, NULL, 3, NULL);
    return err;
}

/* ---------------- registration ---------------- */

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",                  .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/api/sensors",       .method = HTTP_GET,  .handler = api_sensors_get_handler },
        { .uri = "/api/camera",        .method = HTTP_GET,  .handler = api_camera_get_handler },
        { .uri = "/api/camera/flash",  .method = HTTP_POST, .handler = api_camera_flash_post_handler },
        { .uri = "/stream",            .method = HTTP_GET,  .handler = stream_get_handler },
        { .uri = "/api/auth",          .method = HTTP_POST, .handler = api_auth_post_handler },
        { .uri = "/api/status",        .method = HTTP_GET,  .handler = api_status_get_handler },
        { .uri = "/api/led",           .method = HTTP_POST, .handler = api_led_post_handler },
        { .uri = "/api/fans",          .method = HTTP_POST, .handler = api_fans_post_handler },
        { .uri = "/api/schedule",      .method = HTTP_GET,  .handler = api_schedule_get_handler },
        { .uri = "/api/schedule",      .method = HTTP_POST, .handler = api_schedule_post_handler },
        { .uri = "/api/config/wifi",   .method = HTTP_POST, .handler = api_config_wifi_post_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to register %s: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "http server started");
    return ESP_OK;
}
