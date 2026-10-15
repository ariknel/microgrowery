#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fusb302.h"
#include "fusb302_pd.h"
#include "led_control.h"
#include "state.h"

static const char *TAG = "fusb302_pd";

/* USB PD message header (PD 2.0 and 3.0):
 * [4:0]   Message Type
 * [5]     Port Data Role
 * [7:6]   Specification Revision
 * [8]     Port Power Role
 * [11:9]  MessageID
 * [14:12] Number of Data Objects (0 = control message)
 * [15]    Extended */
#define PD_HDR_MSGTYPE(h)   ((h) & 0x1F)
#define PD_HDR_NDO(h)       (((h) >> 12) & 0x07)
#define PD_HDR_EXTENDED(h)  (((h) >> 15) & 0x01)

/* UFP data role (0), spec revision 2.0 (01), sink power role (0). Must agree
 * with SWITCHES1, which the hardware uses for its own GoodCRC headers. A
 * source that sees a data role equal to its own is required to do error
 * recovery, i.e. Hard Reset us. PD 3.0 sources drop to 2.0 for a 2.0 sink. */
#define PD_HDR_BUILD(msgtype, ndo, id) \
    ((uint16_t)(((msgtype) & 0x1F) | (0x1 << 6) | (((id) & 0x07) << 9) | (((ndo) & 0x07) << 12)))

#define PDO_MAX 7

#define RDO_CAP_MISMATCH   (1u << 26)
#define RDO_NO_USB_SUSPEND (1u << 24)

/* Fixed sink PDO: [19:10] voltage in 50mV units, [9:0] current in 10mA. */
#define SNK_PDO_FIXED(mv, ma) \
    (((((uint32_t)(mv) / 50) & 0x3FF) << 10) | (((uint32_t)(ma) / 10) & 0x3FF))
#define SNK_PDO_HIGHER_CAPABILITY (1u << 28)

#define PD_DETACHED_POLL_MS 1000

typedef enum {
    PD_ST_DETACHED = 0,
    PD_ST_WAIT_CAPS,
    PD_ST_REQUEST_SENT,
    PD_ST_NEGOTIATED,
} pd_state_t;

static QueueHandle_t s_pd_queue;

static pd_state_t s_state = PD_ST_DETACHED;
static TickType_t s_next_retry_tick;
static TickType_t s_caps_wait_start;
static TickType_t s_next_get_cap_tick;
static TickType_t s_request_sent_tick;
static uint8_t s_tx_id;
static uint32_t s_req_voltage_mv;
static uint32_t s_req_current_ma;

static bool tick_reached(TickType_t now, TickType_t t) { return (int32_t)(now - t) >= 0; }

static bool pdo_is_fixed(uint32_t pdo) { return ((pdo >> 30) & 0x3) == 0x0; }
static uint32_t pdo_voltage_mv(uint32_t pdo) { return ((pdo >> 10) & 0x3FF) * 50; }
static uint32_t pdo_current_ma(uint32_t pdo) { return (pdo & 0x3FF) * 10; }

static uint32_t build_request_rdo(uint8_t object_position, uint32_t current_ma, bool mismatch)
{
    uint32_t units = (current_ma / 10) & 0x3FF;
    uint32_t rdo = ((uint32_t)(object_position & 0x07) << 28) | RDO_NO_USB_SUSPEND |
                   (units << 10) | units; /* operating == max operating, no GiveBack */
    if (mismatch) rdo |= RDO_CAP_MISMATCH;
    return rdo;
}

static esp_err_t pd_send_message(uint8_t msg_type, const uint32_t *objects, uint8_t ndo)
{
    uint16_t header = PD_HDR_BUILD(msg_type, ndo, s_tx_id);
    uint8_t buf[1 + 4 + 1 + 2 + (4 * PDO_MAX) + 4];
    size_t i = 0;

    /* Token layout matches the Linux fusb302 driver: TXON goes last, so
     * transmission starts only once the whole packet is in the FIFO. */
    buf[i++] = FUSB302_REG_FIFOS;
    buf[i++] = TKN_SYNC1;
    buf[i++] = TKN_SYNC1;
    buf[i++] = TKN_SYNC1;
    buf[i++] = TKN_SYNC2;
    buf[i++] = TKN_PACKSYM | (2 + ndo * 4);
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
    buf[i++] = TKN_TXON;

    fusb302_write_reg(FUSB302_REG_CONTROL0, CONTROL0_TX_FLUSH);
    esp_err_t err = fusb302_write_block(buf, i);
    if (err == ESP_OK) s_tx_id = (s_tx_id + 1) & 0x07;
    return err;
}

/* Reads one packet from the RX FIFO. Returns ESP_ERR_NOT_FOUND when empty. */
static esp_err_t pd_read_message(uint16_t *header, uint32_t *objects, uint8_t *ndo_out)
{
    uint8_t status1 = 0;
    esp_err_t err = fusb302_read_reg(FUSB302_REG_STATUS1, &status1);
    if (err != ESP_OK) return err;
    if (status1 & STATUS1_RX_EMPTY) return ESP_ERR_NOT_FOUND;

    uint8_t token = 0;
    err = fusb302_read_block(FUSB302_REG_FIFOS, &token, 1);
    if (err != ESP_OK) return err;
    if ((token & RX_TKN_MASK) != RX_TKN_SOP) {
        /* Only SOP reception is enabled, so the FIFO is out of step with
         * packet boundaries — drop it rather than misparse what follows. */
        fusb302_write_reg(FUSB302_REG_CONTROL1, CONTROL1_RX_FLUSH);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t hdr[2];
    err = fusb302_read_block(FUSB302_REG_FIFOS, hdr, 2);
    if (err != ESP_OK) return err;
    *header = hdr[0] | ((uint16_t)hdr[1] << 8);

    uint8_t ndo = PD_HDR_NDO(*header);
    *ndo_out = ndo;

    /* Data objects, then the 4-byte CRC the hardware has already checked. */
    uint8_t body[(4 * PDO_MAX) + 4];
    err = fusb302_read_block(FUSB302_REG_FIFOS, body, ndo * 4 + 4);
    if (err != ESP_OK) return err;
    for (uint8_t o = 0; o < ndo; o++) {
        objects[o] = body[o * 4] | ((uint32_t)body[o * 4 + 1] << 8) |
                     ((uint32_t)body[o * 4 + 2] << 16) | ((uint32_t)body[o * 4 + 3] << 24);
    }
    return ESP_OK;
}

/* Picks the 20V fixed PDO if offered, otherwise the highest fixed PDO below
 * it. A source must accept a valid Request for any of its own PDOs, and
 * answering at all is what matters: a sink that stays silent after
 * Source_Capabilities gets Hard Reset (VBUS cut) by the source. */
static bool find_best_pdo(const uint32_t *objects, uint8_t ndo, uint8_t *position_out,
                          uint32_t *voltage_mv_out, uint32_t *current_ma_out)
{
    int best_i = -1;
    uint32_t best_mv = 0;
    for (uint8_t i = 0; i < ndo; i++) {
        if (!pdo_is_fixed(objects[i])) continue;
        uint32_t mv = pdo_voltage_mv(objects[i]);
        if (mv > PD_REQUEST_VOLTAGE_MV || mv <= best_mv) continue;
        best_i = i;
        best_mv = mv;
    }
    if (best_i < 0) return false;
    *position_out = (uint8_t)best_i + 1; /* object position is 1-based */
    *voltage_mv_out = best_mv;
    *current_ma_out = pdo_current_ma(objects[best_i]);
    return true;
}

static void apply_negotiated(uint32_t voltage_mv, uint32_t current_ma)
{
    state_lock();
    g_state.pd_negotiated = true;
    g_state.pd_voltage_mv = voltage_mv;
    g_state.pd_current_ma = current_ma;
    state_unlock();
    /* LEDs and fans only run on a 20V contract (see led_control.c). */
    led_set_power_budget(voltage_mv == PD_REQUEST_VOLTAGE_MV ? voltage_mv * current_ma / 1000 : 0);
    ESP_LOGI(TAG, "PD negotiated: %lu mV @ %lu mA", (unsigned long)voltage_mv, (unsigned long)current_ma);
}

static void clear_negotiated(void)
{
    state_lock();
    bool was_negotiated = g_state.pd_negotiated;
    g_state.pd_negotiated = false;
    g_state.pd_voltage_mv = 0;
    g_state.pd_current_ma = 0;
    state_unlock();
    if (was_negotiated) led_set_power_budget(0);
}

static void enter_wait_caps(TickType_t now, uint32_t get_cap_delay_ms)
{
    s_state = PD_ST_WAIT_CAPS;
    s_caps_wait_start = now;
    s_next_get_cap_tick = now + pdMS_TO_TICKS(get_cap_delay_ms);
}

/* Every reply below is sent before anything is logged: the source only waits
 * tSenderResponse (24-30ms) for an answer, and console output at 115200 baud
 * blocks for several ms per line once the UART FIFO is full. */

static void handle_source_caps(const uint32_t *objects, uint8_t ndo, TickType_t now)
{
    uint8_t pos;
    uint32_t mv, ma;
    if (!find_best_pdo(objects, ndo, &pos, &mv, &ma)) {
        /* Can't happen with a compliant source: PDO 1 is always fixed 5V. */
        ESP_LOGW(TAG, "Source_Capabilities without any fixed PDO");
        return;
    }
    if (ma > PD_REQUEST_MAX_CURRENT_MA) ma = PD_REQUEST_MAX_CURRENT_MA;
    bool mismatch = (mv != PD_REQUEST_VOLTAGE_MV);

    uint32_t rdo = build_request_rdo(pos, ma, mismatch);
    esp_err_t err = pd_send_message(PD_DATA_REQUEST, &rdo, 1);
    if (err == ESP_OK) {
        s_state = PD_ST_REQUEST_SENT;
        s_request_sent_tick = now;
        s_req_voltage_mv = mv;
        s_req_current_ma = ma;
        /* Renegotiating over an existing contract: shed load while VBUS
         * transitions (the spec's pSnkStdby), restored on PS_RDY. */
        clear_negotiated();
    }

    for (uint8_t o = 0; o < ndo; o++) {
        ESP_LOGI(TAG, "  PDO[%d] raw=0x%08lX fixed=%d %lumV %lumA", o + 1, (unsigned long)objects[o],
                 pdo_is_fixed(objects[o]), (unsigned long)pdo_voltage_mv(objects[o]),
                 (unsigned long)pdo_current_ma(objects[o]));
    }
    ESP_LOGI(TAG, "Request PDO %d: %lu mV @ %lu mA%s -> %s", pos, (unsigned long)mv,
             (unsigned long)ma, mismatch ? " (no 20V offered, capability mismatch)" : "",
             esp_err_to_name(err));
}

static void handle_message(uint16_t header, const uint32_t *objects, uint8_t ndo, TickType_t now)
{
    uint8_t type = PD_HDR_MSGTYPE(header);

    /* Extended messages only exist in PD 3.0. */
    if (PD_HDR_EXTENDED(header)) return;

    if (ndo > 0) {
        /* Handled in any state: sources may re-send capabilities after a
         * contract exists (multi-port bricks do when load shifts), and an
         * unanswered one gets a Hard Reset just like the first. */
        if (type == PD_DATA_SOURCE_CAP) {
            handle_source_caps(objects, ndo, now);
        }
        /* Vendor_Defined (Discover Identity etc.), BIST: a PD 2.0 sink
         * ignores data messages it doesn't support. */
        return;
    }

    switch (type) {
    case PD_CTRL_ACCEPT:
        if (s_state == PD_ST_REQUEST_SENT) ESP_LOGI(TAG, "Request accepted, waiting for PS_RDY");
        break;

    case PD_CTRL_PS_RDY:
        if (s_state == PD_ST_REQUEST_SENT) {
            s_state = PD_ST_NEGOTIATED;
            apply_negotiated(s_req_voltage_mv, s_req_current_ma);
        }
        break;

    case PD_CTRL_REJECT:
    case PD_CTRL_WAIT:
        if (s_state == PD_ST_REQUEST_SENT) {
            enter_wait_caps(now, PD_GET_CAP_INTERVAL_MS);
            ESP_LOGW(TAG, "Request %s, asking for capabilities again",
                     type == PD_CTRL_REJECT ? "rejected" : "answered with Wait");
        }
        break;

    case PD_CTRL_SOFT_RESET: {
        /* Accept, and the source re-sends Source_Capabilities. Not answering
         * makes the source escalate to a Hard Reset. */
        s_tx_id = 0;
        esp_err_t err = pd_send_message(PD_CTRL_ACCEPT, NULL, 0);
        enter_wait_caps(now, PD_GET_CAP_DELAY_MS);
        ESP_LOGW(TAG, "Soft_Reset from source, accepted -> %s", esp_err_to_name(err));
        break;
    }

    case PD_CTRL_GET_SNK_CAP: {
        /* Some sources ask right after PS_RDY. The hardware GoodCRCs the
         * request, so silence runs out the source's SenderResponseTimer and
         * it Hard Resets — the "negotiates, then drops VBUS" failure. */
        static const uint32_t snk_caps[] = {
            SNK_PDO_FIXED(5000, 500) | SNK_PDO_HIGHER_CAPABILITY,
            SNK_PDO_FIXED(PD_REQUEST_VOLTAGE_MV, PD_REQUEST_MAX_CURRENT_MA),
        };
        esp_err_t err = pd_send_message(PD_DATA_SINK_CAP, snk_caps, 2);
        ESP_LOGI(TAG, "Get_Sink_Cap -> Sink_Capabilities sent: %s", esp_err_to_name(err));
        break;
    }

    case PD_CTRL_GET_SRC_CAP:
    case PD_CTRL_DR_SWAP:
    case PD_CTRL_PR_SWAP:
    case PD_CTRL_VCONN_SWAP: {
        /* Sink-only, fixed UFP, no VCONN: refuse explicitly (PD 2.0 has no
         * Not_Supported). Silence here gets a Hard Reset too. */
        esp_err_t err = pd_send_message(PD_CTRL_REJECT, NULL, 0);
        ESP_LOGI(TAG, "control message 0x%X refused -> %s", type, esp_err_to_name(err));
        break;
    }

    default:
        /* GoodCRC (the source acknowledging our own messages), Ping,
         * GotoMin (we never offer GiveBack): nothing to send. */
        break;
    }
}

static void drain_rx(TickType_t now)
{
    uint16_t header;
    uint32_t objects[PDO_MAX];
    uint8_t ndo;
    for (int n = 0; n < 8; n++) {
        if (pd_read_message(&header, objects, &ndo) != ESP_OK) break;
        handle_message(header, objects, ndo, now);
        ESP_LOGD(TAG, "rx header=0x%04X type=0x%X ndo=%d state=%d",
                 header, PD_HDR_MSGTYPE(header), ndo, s_state);
    }
}

static void service_interrupts(void)
{
    uint8_t irq = 0, irq_a = 0, irq_b = 0;
    if (fusb302_read_interrupts(&irq, &irq_a, &irq_b) != ESP_OK) return;
    TickType_t now = xTaskGetTickCount();

    if (irq_a & INTERRUPTA_I_HARDRST) {
        /* VBUS goes to 0V and back; whatever is in the FIFO is stale. */
        fusb302_pd_reset();
        s_tx_id = 0;
        clear_negotiated();
        enter_wait_caps(now, PD_GET_CAP_INTERVAL_MS);
        ESP_LOGW(TAG, "Hard Reset from source");
        return;
    }

    if (irq & INTERRUPT_I_VBUSOK) {
        uint8_t status0 = 0;
        if (fusb302_read_reg(FUSB302_REG_STATUS0, &status0) == ESP_OK &&
            !(status0 & STATUS0_VBUSOK) && s_state != PD_ST_DETACHED) {
            clear_negotiated();
            s_state = PD_ST_DETACHED;
            s_next_retry_tick = now + pdMS_TO_TICKS(PD_DETACHED_POLL_MS);
            ESP_LOGW(TAG, "VBUS lost, re-detecting source");
        }
    }

    /* Drain on every interrupt, not only GCRCSENT: GoodCRCs answering our
     * own messages land in the RX FIFO with I_TXSENT instead. */
    drain_rx(now);

    if (irq_a & INTERRUPTA_I_RETRYFAIL) {
        if (s_state == PD_ST_REQUEST_SENT) enter_wait_caps(now, 0);
        ESP_LOGW(TAG, "transmit failed: no GoodCRC from source");
    }
}

static void run_state_machine(TickType_t now)
{
    switch (s_state) {
    case PD_ST_DETACHED:
        if (!tick_reached(now, s_next_retry_tick)) break;
        if (fusb302_detect_cc() == FUSB302_CC_NONE) {
            s_next_retry_tick = now + pdMS_TO_TICKS(PD_DETACHED_POLL_MS);
            break;
        }
        s_tx_id = 0;
        enter_wait_caps(now, PD_GET_CAP_DELAY_MS);
        break;

    case PD_ST_WAIT_CAPS:
        if (tick_reached(now, s_caps_wait_start + pdMS_TO_TICKS(PD_WAIT_CAPS_MS))) {
            ESP_LOGW(TAG, "no Source_Capabilities received, retrying in %d ms", PD_RETRY_INTERVAL_MS);
            s_state = PD_ST_DETACHED;
            s_next_retry_tick = now + pdMS_TO_TICKS(PD_RETRY_INTERVAL_MS);
        } else if (tick_reached(now, s_next_get_cap_tick)) {
            /* Some sources only send their capabilities when asked. The
             * Linux TCPM does the same before giving up; the spec's Hard
             * Reset instead would cut VBUS on a board powered only by this
             * port. */
            esp_err_t err = pd_send_message(PD_CTRL_GET_SRC_CAP, NULL, 0);
            s_next_get_cap_tick = now + pdMS_TO_TICKS(PD_GET_CAP_INTERVAL_MS);
            ESP_LOGI(TAG, "Get_Source_Cap -> %s", esp_err_to_name(err));
        }
        break;

    case PD_ST_REQUEST_SENT:
        /* Accept comes within tSenderResponse, PS_RDY within tPSTransition. */
        if (tick_reached(now, s_request_sent_tick + pdMS_TO_TICKS(1000))) {
            enter_wait_caps(now, 0);
            ESP_LOGW(TAG, "no Accept/PS_RDY for Request, asking for capabilities again");
        }
        break;

    case PD_ST_NEGOTIATED:
        break;
    }
}

static void pd_task(void *arg)
{
    i2c_port_t port = (i2c_port_t)(intptr_t)arg;
    s_pd_queue = xQueueCreate(PD_EVENT_QUEUE_DEPTH, sizeof(fusb302_event_t));

    esp_err_t err = fusb302_init(port, s_pd_queue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fusb302_init failed: %s — PD task idling", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        fusb302_event_t evt;
        xQueueReceive(s_pd_queue, &evt, pdMS_TO_TICKS(50));

        /* INT_N stays low until the interrupt registers are read, but the
         * ISR fires only on the falling edge. If a second interrupt latches
         * while the first is being serviced, the line never rises and no new
         * edge arrives — so service for as long as it's held low. */
        for (int n = 0; n < 8 && fusb302_int_asserted(); n++) {
            service_interrupts();
        }

        run_state_machine(xTaskGetTickCount());
    }
}

void pd_task_start(i2c_port_t i2c_port, UBaseType_t priority, BaseType_t core_id)
{
    xTaskCreatePinnedToCore(pd_task, "pd_task", 4096, (void *)(intptr_t)i2c_port,
                            priority, NULL, core_id);
}
