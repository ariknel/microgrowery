#include <string.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fusb302.h"

static const char *TAG = "fusb302";
static i2c_port_t s_port;
static QueueHandle_t s_event_queue;

esp_err_t fusb302_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(s_port, FUSB302_I2C_ADDR, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

esp_err_t fusb302_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(s_port, FUSB302_I2C_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t fusb302_read_block(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(s_port, FUSB302_I2C_ADDR, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

esp_err_t fusb302_write_block(const uint8_t *buf, size_t len)
{
    /* buf[0] must already be FUSB302_REG_FIFOS; the caller builds the
     * full [reg][token...][token...] sequence in one contiguous buffer
     * so it lands in the FIFO as a single I2C write. */
    return i2c_master_write_to_device(s_port, FUSB302_I2C_ADDR, buf, len, pdMS_TO_TICKS(100));
}

esp_err_t fusb302_reset(void)
{
    esp_err_t err = fusb302_write_reg(FUSB302_REG_RESET, RESET_SW_RES);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t fusb302_read_interrupts(uint8_t *interrupt, uint8_t *interrupt_a, uint8_t *interrupt_b)
{
    /* Status/Interrupt registers 0x3C-0x42 are contiguous; read them in
     * one burst for efficiency, interrupts clear on read. */
    uint8_t buf[7];
    esp_err_t err = fusb302_read_block(FUSB302_REG_STATUS0A, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    /* buf layout: [0]=Status0A [1]=Status1A [2]=InterruptA [3]=InterruptB
     *             [4]=Status0  [5]=Status1  [6]=Interrupt */
    *interrupt_a = buf[2];
    *interrupt_b = buf[3];
    *interrupt = buf[6];
    return ESP_OK;
}

static void IRAM_ATTR fusb302_isr_handler(void *arg)
{
    fusb302_event_t evt = { .irq_snapshot = 1 };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_event_queue, &evt, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static esp_err_t fusb302_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << FUSB302_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, /* pulled up externally per hardware spec */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE means the ISR service is already installed
         * by another driver in this app, which is fine. */
        return err;
    }
    return gpio_isr_handler_add(FUSB302_INT_GPIO, fusb302_isr_handler, NULL);
}

esp_err_t fusb302_init(i2c_port_t i2c_port, QueueHandle_t event_queue)
{
    s_port = i2c_port;
    s_event_queue = event_queue;

    esp_err_t err = fusb302_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t device_id = 0;
    err = fusb302_read_reg(FUSB302_REG_DEVICE_ID, &device_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device id read failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "FUSB302 device id: 0x%02X", device_id);

    /* Power up bandgap, wake, measure and receiver blocks. */
    err = fusb302_write_reg(FUSB302_REG_POWER, POWER_ALL);
    if (err != ESP_OK) return err;

    /* Sink-only: enable pull-downs on both CC lines for detection. */
    err = fusb302_write_reg(FUSB302_REG_SWITCHES0, SWITCHES0_PDWN1 | SWITCHES0_PDWN2);
    if (err != ESP_OK) return err;

    /* Hardware auto-retry on unacked transmissions, 3 retries. */
    err = fusb302_write_reg(FUSB302_REG_CONTROL3,
                             CONTROL3_AUTO_RETRY | CONTROL3_N_RETRIES0 | CONTROL3_N_RETRIES1);
    if (err != ESP_OK) return err;

    /* Mask registers: 0 = interrupt enabled, 1 = masked. Unmask what we
     * actually act on; mask everything else to reduce ISR churn. */
    err = fusb302_write_reg(FUSB302_REG_MASK, (uint8_t)~(INTERRUPT_I_VBUSOK));
    if (err != ESP_OK) return err;
    err = fusb302_write_reg(FUSB302_REG_MASKA,
                             (uint8_t)~(INTERRUPTA_I_TOGDONE | INTERRUPTA_I_RETRYFAIL |
                                        INTERRUPTA_I_TXSENT | INTERRUPTA_I_HARDRST));
    if (err != ESP_OK) return err;
    err = fusb302_write_reg(FUSB302_REG_MASKB, (uint8_t)~(INTERRUPTB_I_GCRCSENT));
    if (err != ESP_OK) return err;

    return fusb302_gpio_init();
}

static uint8_t read_bc_lvl_on(uint8_t meas_bit)
{
    uint8_t sw0 = SWITCHES0_PDWN1 | SWITCHES0_PDWN2 | meas_bit;
    fusb302_write_reg(FUSB302_REG_SWITCHES0, sw0);
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t status0 = 0;
    fusb302_read_reg(FUSB302_REG_STATUS0, &status0);
    return status0 & STATUS0_BC_LVL_MASK;
}

fusb302_cc_t fusb302_detect_cc(void)
{
    uint8_t bc_cc1 = read_bc_lvl_on(SWITCHES0_MEAS_CC1);
    uint8_t bc_cc2 = read_bc_lvl_on(SWITCHES0_MEAS_CC2);

    fusb302_cc_t chosen = FUSB302_CC_NONE;
    if (bc_cc1 > 0 && bc_cc1 >= bc_cc2) {
        chosen = FUSB302_CC1;
    } else if (bc_cc2 > 0) {
        chosen = FUSB302_CC2;
    }

    if (chosen == FUSB302_CC_NONE) {
        return FUSB302_CC_NONE;
    }

    /* Lock measurement + BMC transmit/receive onto the detected line,
     * enable AUTO_CRC, sink power role / UFP data role, PD 2.0. */
    uint8_t meas_bit = (chosen == FUSB302_CC1) ? SWITCHES0_MEAS_CC1 : SWITCHES0_MEAS_CC2;
    fusb302_write_reg(FUSB302_REG_SWITCHES0, SWITCHES0_PDWN1 | SWITCHES0_PDWN2 | meas_bit);

    uint8_t txcc_bit = (chosen == FUSB302_CC1) ? SWITCHES1_TXCC1 : SWITCHES1_TXCC2;
    fusb302_write_reg(FUSB302_REG_SWITCHES1, txcc_bit | SWITCHES1_AUTO_CRC | SWITCHES1_SPECREV0);

    ESP_LOGI(TAG, "attached source on CC%d", chosen);
    return chosen;
}
