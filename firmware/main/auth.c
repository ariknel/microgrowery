#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "auth.h"
#include "nvs_config.h"

static const char *TAG = "auth";

typedef struct {
    bool in_use;
    char token[AUTH_TOKEN_LEN + 1];
    int64_t expires_at_us;
} auth_token_slot_t;

static auth_token_slot_t s_tokens[AUTH_MAX_TOKENS];
static char s_auth_code[16];

esp_err_t auth_init(void)
{
    memset(s_tokens, 0, sizeof(s_tokens));
    return nvs_config_get_str("auth_code", s_auth_code, sizeof(s_auth_code), "4712");
}

static void hex_token(char *out)
{
    static const char hexchars[] = "0123456789abcdef";
    uint8_t raw[AUTH_TOKEN_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2] = hexchars[raw[i] >> 4];
        out[i * 2 + 1] = hexchars[raw[i] & 0x0F];
    }
    out[AUTH_TOKEN_LEN] = '\0';
}

bool auth_check_code(const char *code, char *token_out)
{
    if (strcmp(code, s_auth_code) != 0) {
        ESP_LOGW(TAG, "auth attempt rejected");
        return false;
    }

    /* Find a free slot, or the oldest one if the table is full. */
    int slot = -1;
    int64_t oldest = INT64_MAX;
    int oldest_idx = 0;
    int64_t now = esp_timer_get_time();

    for (int i = 0; i < AUTH_MAX_TOKENS; i++) {
        if (!s_tokens[i].in_use || s_tokens[i].expires_at_us < now) {
            slot = i;
            break;
        }
        if (s_tokens[i].expires_at_us < oldest) {
            oldest = s_tokens[i].expires_at_us;
            oldest_idx = i;
        }
    }
    if (slot < 0) slot = oldest_idx;

    hex_token(s_tokens[slot].token);
    s_tokens[slot].in_use = true;
    s_tokens[slot].expires_at_us = now + (int64_t)AUTH_TOKEN_TTL_MS * 1000;

    strncpy(token_out, s_tokens[slot].token, AUTH_TOKEN_LEN + 1);
    ESP_LOGI(TAG, "auth success, token issued");
    return true;
}

bool auth_check_token(const char *token)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < AUTH_MAX_TOKENS; i++) {
        if (s_tokens[i].in_use && strcmp(s_tokens[i].token, token) == 0) {
            if (s_tokens[i].expires_at_us < now) {
                s_tokens[i].in_use = false;
                return false;
            }
            return true;
        }
    }
    return false;
}
