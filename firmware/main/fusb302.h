#pragma once

/* FUSB302B register-level driver.
 *
 * Register addresses below are the authoritative FUSB302B map (datasheet
 * Rev. as sold by onsemi/Fairchild). Bit-field defines are transcribed to
 * the best of available reference material; verify against your exact
 * FUSB302 datasheet revision before relying on them on real hardware —
 * USB PD signaling is unforgiving of an incorrectly-set bit.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define FUSB302_I2C_ADDR   0x22
#define FUSB302_INT_GPIO   GPIO_NUM_10

/* ---- register addresses ---- */
#define FUSB302_REG_DEVICE_ID   0x01
#define FUSB302_REG_SWITCHES0   0x02
#define FUSB302_REG_SWITCHES1   0x03
#define FUSB302_REG_MEASURE     0x04
#define FUSB302_REG_SLICE       0x05
#define FUSB302_REG_CONTROL0    0x06
#define FUSB302_REG_CONTROL1    0x07
#define FUSB302_REG_CONTROL2    0x08
#define FUSB302_REG_CONTROL3    0x09
#define FUSB302_REG_MASK        0x0A
#define FUSB302_REG_POWER       0x0B
#define FUSB302_REG_RESET       0x0C
#define FUSB302_REG_OCPREG      0x0D
#define FUSB302_REG_MASKA       0x0E
#define FUSB302_REG_MASKB       0x0F
#define FUSB302_REG_CONTROL4    0x10
#define FUSB302_REG_STATUS0A    0x3C
#define FUSB302_REG_STATUS1A    0x3D
#define FUSB302_REG_INTERRUPTA  0x3E
#define FUSB302_REG_INTERRUPTB  0x3F
#define FUSB302_REG_STATUS0     0x40
#define FUSB302_REG_STATUS1     0x41
#define FUSB302_REG_INTERRUPT   0x42
#define FUSB302_REG_FIFOS       0x43

/* ---- Switches0 bits ---- */
#define SWITCHES0_PDWN1     (1 << 0)
#define SWITCHES0_PDWN2     (1 << 1)
#define SWITCHES0_MEAS_CC1  (1 << 2)
#define SWITCHES0_MEAS_CC2  (1 << 3)
#define SWITCHES0_VCONN_CC1 (1 << 4)
#define SWITCHES0_VCONN_CC2 (1 << 5)
#define SWITCHES0_PU_EN1    (1 << 6)
#define SWITCHES0_PU_EN2    (1 << 7)

/* ---- Switches1 bits ---- */
#define SWITCHES1_TXCC1      (1 << 0)
#define SWITCHES1_TXCC2      (1 << 1)
#define SWITCHES1_AUTO_CRC   (1 << 2)
#define SWITCHES1_DATAROLE   (1 << 4)
#define SWITCHES1_SPECREV0   (1 << 5)
#define SWITCHES1_SPECREV1   (1 << 6)
#define SWITCHES1_POWERROLE  (1 << 7)

/* ---- Control0 bits ---- */
#define CONTROL0_TX_START    (1 << 0)
#define CONTROL0_AUTO_PRE    (1 << 1)
#define CONTROL0_HOST_CUR0   (1 << 2)
#define CONTROL0_HOST_CUR1   (1 << 3)
#define CONTROL0_INT_MASK    (1 << 5)
#define CONTROL0_TX_FLUSH    (1 << 6)

/* ---- Control1 bits ---- */
#define CONTROL1_ENSOP1      (1 << 0)
#define CONTROL1_ENSOP2      (1 << 1)
#define CONTROL1_RX_FLUSH    (1 << 2)
#define CONTROL1_BIST_MODE2  (1 << 4)
#define CONTROL1_ENSOP1DB    (1 << 5)
#define CONTROL1_ENSOP2DB    (1 << 6)

/* ---- Control3 bits ---- */
#define CONTROL3_AUTO_RETRY   (1 << 0)
#define CONTROL3_N_RETRIES0   (1 << 1)
#define CONTROL3_N_RETRIES1   (1 << 2)
#define CONTROL3_AUTO_SOFTRST (1 << 3)
#define CONTROL3_AUTO_HARDRST (1 << 4)
#define CONTROL3_SEND_HARDRST (1 << 6)

/* ---- Power register bits (power up blocks) ---- */
#define POWER_PWR_BANDGAP_WAKE (1 << 0)
#define POWER_PWR_RECEIVER     (1 << 1)
#define POWER_PWR_MEASURE      (1 << 2)
#define POWER_PWR_INTOSC       (1 << 3)
#define POWER_ALL              0x0F

/* ---- Reset register bits ---- */
#define RESET_SW_RES   (1 << 0)
#define RESET_PD_RESET (1 << 1)

/* ---- Status0 bits ---- */
#define STATUS0_BC_LVL_MASK  0x03
#define STATUS0_WAKE_ALL     (1 << 2)
#define STATUS0_ACTIVITY     (1 << 3)
#define STATUS0_COMP         (1 << 4)
#define STATUS0_CRC_CHK      (1 << 5)
#define STATUS0_ALERT        (1 << 6)
#define STATUS0_VBUSOK       (1 << 7)

/* ---- InterruptA bits ---- */
#define INTERRUPTA_I_HARDRST   (1 << 0)
#define INTERRUPTA_I_TXSENT    (1 << 1)
#define INTERRUPTA_I_SOFTFAIL  (1 << 4)
#define INTERRUPTA_I_RETRYFAIL (1 << 5)
#define INTERRUPTA_I_TOGDONE   (1 << 6)

/* ---- InterruptB bits ---- */
#define INTERRUPTB_I_GCRCSENT  (1 << 0)

/* ---- Interrupt bits ---- */
#define INTERRUPT_I_BC_LVL     (1 << 0)
#define INTERRUPT_I_COLLISION  (1 << 1)
#define INTERRUPT_I_VBUSOK     (1 << 7)

/* ---- FIFO tokens ---- */
#define TKN_TXON     0xA1
#define TKN_SOP1     0x12
#define TKN_SOP2     0x12
#define TKN_SOP3     0x12
#define TKN_SOP_END  0x13
#define TKN_PACKSYM  0x80  /* OR with byte count (0-31) */
#define TKN_JAMCRC   0xFF
#define TKN_EOP      0x14
#define TKN_TXOFF    0xFE

typedef enum {
    FUSB302_CC_NONE = 0,
    FUSB302_CC1,
    FUSB302_CC2,
} fusb302_cc_t;

typedef struct {
    uint8_t irq_snapshot; /* placeholder payload, real content read in pd_task */
} fusb302_event_t;

esp_err_t fusb302_init(i2c_port_t i2c_port, QueueHandle_t event_queue);
esp_err_t fusb302_reset(void);
esp_err_t fusb302_read_reg(uint8_t reg, uint8_t *val);
esp_err_t fusb302_write_reg(uint8_t reg, uint8_t val);
esp_err_t fusb302_read_block(uint8_t reg, uint8_t *buf, size_t len);
esp_err_t fusb302_write_block(const uint8_t *buf, size_t len);

/* Detects which CC line the source is attached on and locks measurement +
 * transmit routing onto it. Returns FUSB302_CC_NONE if nothing attached. */
fusb302_cc_t fusb302_detect_cc(void);

esp_err_t fusb302_read_interrupts(uint8_t *interrupt, uint8_t *interrupt_a, uint8_t *interrupt_b);
