#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fusb302.h"
#include "fusb302_pd.h"
#include "state.h"

static const char *TAG = "fusb302_pd";

/* ---- PD message header bit layout (USB PD 2.0 spec) ----
 * [3:0]   Message Type
 * [4]     Port Data Role   (0 = UFP, fixed — we never data-role-swap)
 * [6:5]   Specification Revision (01 = PD 2.0)
 * [7]     Port Power Role  (0 = Sink, fixed — sink-only device)
 * [10:8]  MessageID
 * [13:11] Number of Data Objects (0 = control message)
 * [14]    Reserved
 * [15]    Extended (0, not implemented)
 */
#define PD_HDR_MSGTYPE(h)   ((h) & 0x0F)
#define PD_HDR_NDO(h)       (((h) >> 11) & 0x07)
#define PD_HDR_BUILD(msgtype, ndo, message_id) \
    ((uint16_t)((msgtype) & 0x0F) | \
     ((uint16_t)((message_id) & 0x07) << 8) | \
     ((uint16_t)((ndo) & 0x07) << 11) | \
     (0x01 << 5)) /* SpecRev = PD 2.0, PowerRole = sink, DataRole = UFP */

#define PDO_MAX 7

typedef enum {
    PD_ST_DETACHED = 0,
    PD_ST_WAIT_CAPS,
    PD_ST_REQUEST_SENT,
    PD_ST_NEGOTIATED,
} pd_state_t;

static i2c_port_t s_i2c_port;
static QueueHandle_t s_pd_queue;
static uint8_t s_next_message_id = 0;

static bool pdo_is_fixed(uint32_t pdo) { return ((pdo >> 30) & 0x3) == 0x0; }
static uint32_t pdo_voltage_mv(uint32_t pdo) { return ((pdo >> 10) & 0x3FF) * 50; }
static uint32_t pdo_current_ma(uint32_t pdo) { return (pdo & 0x3FF) * 10; }

static uint32_t build_request_rdo(uint8_t object_position, uint32_t current_ma)
{
    uint32_t units = current_ma / 10;
    uint32_t rdo = 0;
    rdo |= ((uint32_t)(object_position & 0x0F)) << 28;
    rdo |= (1u << 24); /* No USB Suspend */
    rdo |= (units & 0x3FF) << 10; /* operating current */
    rdo |= (units & 0x3FF);       /* max operating current (== operating, no GiveBack) */
    return rdo;
}

static esp_err_t pd_send_message(uint8_t msg_type, const uint32_t *objects, uint8_t ndo)
{
    uint16_t header = PD_HDR_BUILD(msg_type, ndo, s_next_message_id);
    s_next_message_id = (s_next_message_id + 1) & 0x07;

    uint8_t byte_count = 2 + (ndo * 4);
    uint8_t buf[1 + 1 + 4 + 1 + 2 + (4 * PDO_MAX) + 1 + 1 + 1];
    size_t i = 0;

    buf[i++] = FUSB302_REG_FIFOS;
    buf[i++] = TKN_TXON;
    buf[i++] = TKN_SOP1;
    buf[i++] = TKN_SOP1;
    buf[i++] = TKN_SOP1;
    buf[i++] = TKN_SOP_END;
    buf[i++] = TKN_PACKSYM | byte_count;
    buf[i++] = header & 0xFF;
    buf[i++] = (header >> 8) & 0xFF;
    for (uint8_t o = 0; o < ndo; o++) {
        buf[i++] = objects[o] & 0xFF;
        buf[i++] = (objects[o] >> 8) & 0xFF;
        buf[i++] = (objects[o] >> 16) & 0xFF;
        buf[i++] = (objects[o] >> 24) & 0xFF;
    }
    buf[i++] = TKN_JAMCRC;
    buf[i++] = TKN_EOP;
    buf[i++] = TKN_TXOFF;

    esp_err_t err = fusb302_write_block(buf, i);
    if (err != ESP_OK) return err;

    return fusb302_write_reg(FUSB302_REG_CONTROL0, CONTROL0_TX_START);
}

/* Reads one received message from the FIFO, if one is waiting.
 * Returns ESP_ERR_NOT_FOUND if the FIFO doesn't currently hold a packet. */
static esp_err_t pd_read_message(uint16_t *header, uint32_t *objects, uint8_t *ndo_out)
{
    uint8_t token = 0;
    esp_err_t err = fusb302_read_block(FUSB302_REG_FIFOS, &token, 1);
    if (err != ESP_OK) return err;

    if ((token & 0xE0) != 0xE0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t hdr_bytes[2];
    err = fusb302_read_block(FUSB302_REG_FIFOS, hdr_bytes, 2);
    if (err != ESP_OK) return err;

    uint16_t hdr = hdr_bytes[0] | ((uint16_t)hdr_bytes[1] << 8);
    *header = hdr;

    uint8_t ndo = PD_HDR_NDO(hdr);
    if (ndo > PDO_MAX) ndo = PDO_MAX;
    *ndo_out = ndo;

    if (ndo > 0) {
        uint8_t obj_bytes[4 * PDO_MAX];
        err = fusb302_read_block(FUSB302_REG_FIFOS, obj_bytes, ndo * 4);
        if (err != ESP_OK) return err;
        for (uint8_t o = 0; o < ndo; o++) {
            objects[o] = obj_bytes[o * 4] | ((uint32_t)obj_bytes[o * 4 + 1] << 8) |
                         ((uint32_t)obj_bytes[o * 4 + 2] << 16) | ((uint32_t)obj_bytes[o * 4 + 3] << 24);
        }
    }

    uint8_t crc[4];
    fusb302_read_block(FUSB302_REG_FIFOS, crc, 4); /* drain trailer, hardware already checked it */

    return ESP_OK;
}

static bool find_20v_pdo(const uint32_t *objects, uint8_t ndo, uint8_t *position_out, uint32_t *current_ma_out)
{
    for (uint8_t i = 0; i < ndo; i++) {
        if (pdo_is_fixed(objects[i]) && pdo_voltage_mv(objects[i]) == PD_REQUEST_VOLTAGE_MV) {
            *position_out = i + 1; /* object position is 1-based */
            *current_ma_out = pdo_current_ma(objects[i]);
            return true;
        }
    }
    return false;
}

static void apply_negotiated(uint32_t voltage_mv, uint32_t current_ma)
{
    state_lock();
    g_state.pd_negotiated = true;
    g_state.pd_voltage_mv = voltage_mv;
    g_state.pd_current_ma = current_ma;
    state_unlock();
    ESP_LOGI(TAG, "PD negotiated: %lu mV @ %lu mA", (unsigned long)voltage_mv, (unsigned long)current_ma);
}

static void clear_negotiated(void)
{
    state_lock();
    g_state.pd_negotiated = false;
    g_state.pd_voltage_mv = 0;
    g_state.pd_current_ma = 0;
    state_unlock();
}

static void pd_task(void *arg)
{
    s_i2c_port = (i2c_port_t)(intptr_t)arg;
    s_pd_queue = xQueueCreate(PD_EVENT_QUEUE_DEPTH, sizeof(fusb302_event_t));

    esp_err_t err = fusb302_init(s_i2c_port, s_pd_queue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fusb302_init failed: %s — PD task idling", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    pd_state_t pd_state = PD_ST_DETACHED;
    TickType_t next_retry_tick = 0;
    TickType_t caps_wait_start = 0;
    TickType_t request_sent_tick = 0;
    uint8_t req_object_position = 0;
    uint32_t req_current_ma = 0;
    uint32_t req_voltage_mv = 0;

    for (;;) {
        fusb302_event_t evt;
        BaseType_t got = xQueueReceive(s_pd_queue, &evt, pdMS_TO_TICKS(500));
        TickType_t now = xTaskGetTickCount();

        if (got) {
            uint8_t irq = 0, irq_a = 0, irq_b = 0;
            if (fusb302_read_interrupts(&irq, &irq_a, &irq_b) == ESP_OK) {

                if (irq & INTERRUPT_I_VBUSOK) {
                    uint8_t status0 = 0;
                    fusb302_read_reg(FUSB302_REG_STATUS0, &status0);
                    if (!(status0 & STATUS0_VBUSOK) && pd_state != PD_ST_DETACHED) {
                        ESP_LOGW(TAG, "VBUS lost, re-detecting source");
                        clear_negotiated();
                        pd_state = PD_ST_DETACHED;
                        next_retry_tick = now; /* reconnect immediately, no 30s wait for a real unplug */
                    }
                }

                if (irq_b & INTERRUPTB_I_GCRCSENT) {
                    uint16_t header;
                    uint32_t objects[PDO_MAX];
                    uint8_t ndo;
                    while (pd_read_message(&header, objects, &ndo) == ESP_OK) {
                        uint8_t msg_type = PD_HDR_MSGTYPE(header);

                        if (pd_state == PD_ST_WAIT_CAPS && ndo > 0 && msg_type == PD_DATA_SOURCE_CAP) {
                            if (find_20v_pdo(objects, ndo, &req_object_position, &req_current_ma)) {
                                req_current_ma = req_current_ma < PD_REQUEST_MAX_CURRENT_MA
                                                     ? req_current_ma : PD_REQUEST_MAX_CURRENT_MA;
                                req_voltage_mv = PD_REQUEST_VOLTAGE_MV;
                                uint32_t rdo = build_request_rdo(req_object_position, req_current_ma);
                                if (pd_send_message(PD_DATA_REQUEST, &rdo, 1) == ESP_OK) {
                                    ESP_LOGI(TAG, "sent Request: pos=%d, %lu mA",
                                             req_object_position, (unsigned long)req_current_ma);
                                    pd_state = PD_ST_REQUEST_SENT;
                                    request_sent_tick = now;
                                }
                            } else {
                                ESP_LOGW(TAG, "no 20V fixed PDO advertised by source, will retry in 30s");
                                pd_state = PD_ST_DETACHED;
                                next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
                            }
                        } else if (pd_state == PD_ST_REQUEST_SENT && ndo == 0 && msg_type == PD_CTRL_ACCEPT) {
                            ESP_LOGI(TAG, "Request accepted, waiting for PS_RDY");
                        } else if (pd_state == PD_ST_REQUEST_SENT && ndo == 0 && msg_type == PD_CTRL_PS_RDY) {
                            apply_negotiated(req_voltage_mv, req_current_ma);
                            pd_state = PD_ST_NEGOTIATED;
                        } else if (pd_state == PD_ST_REQUEST_SENT && ndo == 0 &&
                                   (msg_type == PD_CTRL_REJECT || msg_type == PD_CTRL_WAIT)) {
                            ESP_LOGW(TAG, "Request rejected/waited, retrying in 30s");
                            pd_state = PD_ST_DETACHED;
                            next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
                        }
                    }
                }

                if (irq_a & INTERRUPTA_I_RETRYFAIL) {
                    ESP_LOGW(TAG, "hardware retry limit hit, retrying in 30s");
                    clear_negotiated();
                    pd_state = PD_ST_DETACHED;
                    next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
                }
            }
        }

        switch (pd_state) {
        case PD_ST_DETACHED:
            if (now >= next_retry_tick) {
                if (fusb302_detect_cc() != FUSB302_CC_NONE) {
                    pd_state = PD_ST_WAIT_CAPS;
                    caps_wait_start = now;
                } else {
                    next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
                }
            }
            break;

        case PD_ST_WAIT_CAPS:
            if (now - caps_wait_start > pdMS_TO_TICKS(5000)) {
                ESP_LOGW(TAG, "no Source_Capabilities received, retrying in 30s");
                pd_state = PD_ST_DETACHED;
                next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
            }
            break;

        case PD_ST_REQUEST_SENT:
            if (now - request_sent_tick > pdMS_TO_TICKS(1000)) {
                ESP_LOGW(TAG, "no response to Request, retrying in 30s");
                pd_state = PD_ST_DETACHED;
                next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
            }
            break;

        case PD_ST_NEGOTIATED:
            /* Steady state; VBUS-loss interrupt handling above drives us
             * back to PD_ST_DETACHED on disconnect. */
            break;
        }
    }
}

void pd_task_start(i2c_port_t i2c_port, UBaseType_t priority, BaseType_t core_id)
{
    xTaskCreatePinnedToCore(pd_task, "pd_task", 4096, (void *)(intptr_t)i2c_port,
                             priority, NULL, core_id);
}
