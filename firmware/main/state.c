#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "state.h"

growbox_state_t g_state;
static SemaphoreHandle_t s_state_mutex;

void state_init(void)
{
    s_state_mutex = xSemaphoreCreateMutex();
    memset(&g_state, 0, sizeof(g_state));
    g_state.temp_min = 1000.0f;
    g_state.temp_max = -1000.0f;
    g_state.humidity_min = 1000.0f;
    g_state.humidity_max = -1000.0f;
    strncpy(g_state.firmware_version, "1.0.0", FW_VERSION_MAXLEN - 1);
}

void state_lock(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
}

void state_unlock(void)
{
    xSemaphoreGive(s_state_mutex);
}
