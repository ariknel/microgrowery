/* GrowBox LED cooling fan controller — CH32F4P6 (WCH, ARM Cortex-M4F)
 *
 * Independent thermal safety loop for the 8x 5015 blowers cooling the LED
 * heatsinks (see ../BUILD.md#thermal-analysis--led-board-cooling). Runs on
 * its own MCU specifically so fan control keeps working even if the
 * ESP32-S3 hub or ESP32-CAM hang, crash, or are mid-reboot — it has no
 * dependency on either board.
 *
 * Custom PCB (not the earlier Nano perfboard prototype): CH32F4P6 powered
 * from the hub's 12V rail through an onboard AMS1117-3.3, driving 4x
 * Si2302 N-channel MOSFETs (one per LED board's pair of fans) directly
 * from GPIO — Si2302 is logic-level enough at 3.3V Vgs for this load (a
 * couple hundred mA per pair), no gate driver needed.
 *
 * IMPORTANT — written without WCH's actual EVT header files in hand. This
 * targets WCH's Standard-Peripheral-Library-style API (CH32F4 is marketed
 * as register/pin-compatible with STM32F405, and WCH's library follows
 * STM32 SPL conventions closely), but exact function/macro names should
 * be verified against your real Libraries/Peripheral/inc once it's
 * vendored in — see README.md. Flagged inline wherever a call is a
 * best-effort guess rather than a known-correct API.
 *
 * Pinout (from the real schematic):
 *   PWM_1  PD2  TIM1_CH1      NTC_1  PA2  ADC_Channel_0
 *   PWM_2  PA1  TIM1_CH2      NTC_2  PD3  ADC_Channel_4
 *   PWM_3  PC3  TIM1_CH3      NTC_3  PD4  ADC_Channel_7
 *   PWM_4  PC4  TIM1_CH4      NTC_4  PD6  ADC_Channel_6
 *   SWIO   PD1  programming/debug — not touched by application code
 *
 * ADC channel numbers above are taken directly from the pinout table as
 * given, not re-derived from a CH32F4 pin-to-channel map — if readings
 * come back wrong, that table is the first thing to re-check against the
 * datasheet.
 *
 * NOT implemented yet: the Nano version's UART report-to-hub (hub's J14
 * UART0, see ../fan_controller's git history / ../BUILD.md). No TX pin
 * for that was included in this pinout, so it's left out here rather than
 * guessing a pin on a real, already-laid-out PCB. Cooling itself doesn't
 * depend on it — same as before, this is read-only visibility, not a
 * control input.
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "ch32f4xx.h"
#include "ch32f4xx_conf.h"

/* ---------------- configuration ---------------- */

#define NUM_ZONES 4

/* NTC voltage divider: 3.3V -> NTC -> (sense node) -> SERIES_RESISTOR -> GND.
 * Same values as the original Nano design — confirmed unchanged on the
 * new board: NTC is still 100k, and the divider is ratiometric (supply
 * and ADC reference are the same 3.3V rail off the onboard AMS1117), so
 * the 5V->3.3V supply change doesn't itself require a different series
 * resistor. ADC_MAX changed from 1023 (Nano's 10-bit ADC) to 4095
 * (CH32F4's 12-bit ADC) — that's the part of this port that's actually
 * resolution-driven, not voltage-driven. */
#define SERIES_RESISTOR_OHMS 47000.0f
#define NTC_NOMINAL_OHMS     100000.0f
#define NTC_NOMINAL_C        25.0f
#define NTC_BETA             3950.0f
#define ADC_MAX               4095.0f

/* Fan speed curve, per zone — identical logic to the Nano version. Below
 * FAN_START_C that zone's fans are fully off; duty ramps linearly from
 * MIN_DUTY at FAN_START_C to full at FAN_FULL_C and stays full above
 * that. FAN_OFF_HYST_C keeps the curve from chattering at the threshold.
 * Still placeholders — tune against a real thermocouple on the board. */
#define FAN_START_C    35.0f
#define FAN_FULL_C     55.0f
#define FAN_OFF_HYST_C  3.0f
#define FAN_MIN_DUTY_PCT 35   /* of PWM_PERIOD — enough to spin a small blower reliably */

/* A sensor reading this far outside plausible range (wiring fault, open
 * circuit, short) is treated as a FAILURE for that zone — fails toward
 * fans-on (full speed), not fans-off, since this is a cooling safety
 * loop. Other zones are unaffected. */
#define SENSOR_FAULT_LOW_C   -20.0f
#define SENSOR_FAULT_HIGH_C  120.0f

#define SAMPLE_INTERVAL_MS 500

/* TIM1 PWM frequency target: 25kHz, above the audible range (matches the
 * LED PWM frequency used on the hub) — avoids coil whine on the small
 * blowers. PWM_PERIOD (TIM1 ARR) is derived from an assumed APB2 timer
 * clock; VERIFY this against whatever SystemInit() (vendored, unmodified
 * from WCH's EVT) actually configures for your board's crystal — if it's
 * not 168MHz, PWM_FREQ_HZ below will be wrong until TIM1_CLOCK_HZ is
 * corrected to match. */
#define TIM1_CLOCK_HZ   168000000UL
#define PWM_FREQ_HZ     25000UL
#define PWM_PERIOD      (TIM1_CLOCK_HZ / PWM_FREQ_HZ)   /* TIM1 prescaler left at 0 */

/* ---------------- millis()-equivalent ---------------- */

extern volatile uint32_t g_tick_ms; /* incremented in ch32f4xx_it.c's SysTick_Handler */
static inline uint32_t millis(void) { return g_tick_ms; }

/* ---------------- state ---------------- */

static const uint8_t ADC_CHANNELS[NUM_ZONES] = {
    ADC_Channel_0, /* NTC_1, PA2 */
    ADC_Channel_4, /* NTC_2, PD3 */
    ADC_Channel_7, /* NTC_3, PD4 */
    ADC_Channel_6, /* NTC_4, PD6 */
};

static bool zoneFansRunning[NUM_ZONES] = { false, false, false, false };

/* ---------------- NTC read ---------------- */

/* Blocking single-conversion read on ADC1's regular channel — simplest
 * correct approach for 4 channels sampled every 500ms; no need for
 * DMA/scan mode at this update rate. Re-selects the channel each call via
 * ADC_RegularChannelConfig (rank 1), matching the SPL pattern for
 * software-triggered single conversions. */
static uint16_t adc_read_raw(uint8_t channel)
{
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) { }
    return ADC_GetConversionValue(ADC1);
}

static float readTempC(uint8_t channel)
{
    int raw = adc_read_raw(channel);

    /* Open circuit (NTC unplugged) reads near ADC_MAX; a dead short reads
     * near 0 — both physically impossible real temperatures, caught
     * before the Steinhart-Hart math turns them into a plausible-looking
     * but meaningless number. */
    if (raw <= 1 || raw >= (int)ADC_MAX - 1) {
        return NAN;
    }

    float resistance = SERIES_RESISTOR_OHMS * (ADC_MAX / (float)raw - 1.0f);

    /* Simplified beta equation — accurate enough for a fan-curve input,
     * not lab-grade calibration. Identical to the Nano version. */
    float steinhart = resistance / NTC_NOMINAL_OHMS;
    steinhart = logf(steinhart);
    steinhart /= NTC_BETA;
    steinhart += 1.0f / (NTC_NOMINAL_C + 273.15f);
    steinhart = 1.0f / steinhart;
    steinhart -= 273.15f;

    if (steinhart < SENSOR_FAULT_LOW_C || steinhart > SENSOR_FAULT_HIGH_C) {
        return NAN;
    }
    return steinhart;
}

/* ---------------- fan curve ---------------- */

static uint32_t dutyForZone(uint8_t zone, float tempC)
{
    uint32_t minDuty = (PWM_PERIOD * FAN_MIN_DUTY_PCT) / 100;

    /* NAN means this zone's sensor is faulty — fail safe to full speed
     * for this zone only, rather than trusting a bogus "cold" reading. */
    if (isnan(tempC)) {
        zoneFansRunning[zone] = true;
        return PWM_PERIOD;
    }

    float startThreshold = zoneFansRunning[zone] ? (FAN_START_C - FAN_OFF_HYST_C) : FAN_START_C;

    if (tempC < startThreshold) {
        zoneFansRunning[zone] = false;
        return 0;
    }
    zoneFansRunning[zone] = true;

    if (tempC >= FAN_FULL_C) return PWM_PERIOD;

    float span = FAN_FULL_C - FAN_START_C;
    float frac = (tempC - FAN_START_C) / span;
    if (frac < 0) frac = 0;

    uint32_t duty = minDuty + (uint32_t)(frac * (float)(PWM_PERIOD - minDuty));
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    if (duty < minDuty) duty = minDuty;
    return duty;
}

/* ---------------- peripheral init ----------------
 * GPIO alternate-function config (GPIO_PinAFConfig + GPIO_Mode_AF) below
 * follows STM32F4-style AF selection, matching CH32F4's advertised
 * STM32F405 compatibility. WCH has in the past kept STM32F1-style AFIO
 * remap (GPIO_PinRemapConfig) on some product lines instead — if the
 * build fails on GPIO_PinAFConfig/GPIO_AF_TIM1, that's the fallback to
 * try. Same caveat for GPIO_Mode_AIN (analog input) below. */

static void gpio_init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);

    GPIO_InitTypeDef gpio = { 0 };

    /* PWM outputs — alternate function push-pull, TIM1 channels 1-4 */
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource2, GPIO_AF_TIM1); /* PD2 = PWM_1 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_TIM1); /* PA1 = PWM_2 */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3, GPIO_AF_TIM1); /* PC3 = PWM_3 */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_TIM1); /* PC4 = PWM_4 */

    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    gpio.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOD, &gpio); /* PWM_1 */
    gpio.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &gpio); /* PWM_2 */
    gpio.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOC, &gpio); /* PWM_3 */
    gpio.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOC, &gpio); /* PWM_4 */

    /* NTC analog inputs */
    GPIO_InitTypeDef adc_gpio = { 0 };
    adc_gpio.GPIO_Mode = GPIO_Mode_AIN;
    adc_gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;

    adc_gpio.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOA, &adc_gpio); /* NTC_1, PA2 */
    adc_gpio.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOD, &adc_gpio); /* NTC_2, PD3 */
    adc_gpio.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOD, &adc_gpio); /* NTC_3, PD4 */
    adc_gpio.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOD, &adc_gpio); /* NTC_4, PD6 */

    /* PD1 (SWIO) is left untouched — programming/debug only. */
}

static void adc_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    ADC_InitTypeDef adc = { 0 };
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);
}

/* TIM1 is an advanced-control timer (shared with STM32F4's TIM1) — needs
 * TIM_CtrlPWMOutputs() (the "main output enable" bit) in addition to the
 * usual OC/Cmd sequence that basic timers use, or the PWM outputs stay
 * dark even though everything else looks configured correctly. Easy to
 * lose an hour to if this call is missing. */
static void pwm_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInitTypeDef tb = { 0 };
    tb.TIM_Prescaler = 0;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = PWM_PERIOD - 1;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM_OCInitTypeDef oc = { 0 };
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_Pulse = 0;

    TIM_OC1Init(TIM1, &oc);
    TIM_OC2Init(TIM1, &oc);
    TIM_OC3Init(TIM1, &oc);
    TIM_OC4Init(TIM1, &oc);

    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE); /* advanced-timer main output enable — see comment above */
    TIM_Cmd(TIM1, ENABLE);
}

static void set_duty(uint8_t zone, uint32_t duty)
{
    switch (zone) {
        case 0: TIM_SetCompare1(TIM1, duty); break;
        case 1: TIM_SetCompare2(TIM1, duty); break;
        case 2: TIM_SetCompare3(TIM1, duty); break;
        case 3: TIM_SetCompare4(TIM1, duty); break;
    }
}

int main(void)
{
    /* SystemInit() runs before main() from the vendored startup file —
     * clock tree (PLL from HSE, AHB/APB prescalers) is already up by the
     * time we get here. */
    SysTick_Config(SystemCoreClock / 1000); /* 1ms tick -> millis() */

    gpio_init();
    adc_init();
    pwm_init();

    uint32_t lastSampleMs = 0;

    for (;;) {
        uint32_t now = millis();
        if (now - lastSampleMs < SAMPLE_INTERVAL_MS) continue;
        lastSampleMs = now;

        for (uint8_t z = 0; z < NUM_ZONES; z++) {
            float tempC = readTempC(ADC_CHANNELS[z]);
            uint32_t duty = dutyForZone(z, tempC);
            set_duty(z, duty);
        }
    }
}
