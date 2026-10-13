#include <stdint.h>
#include "ch32f4xx.h"
#include "ch32f4xx_it.h"

/* millis()-equivalent tick, incremented here, read via millis() in
 * main.c. SysTick is configured in main() via SysTick_Config(). */
volatile uint32_t g_tick_ms = 0;

void NMI_Handler(void) { }

/* Parked here deliberately rather than returning — a hard fault on a
 * device switching 12V fan power is not a state to limp on from. This
 * does NOT force the fans on; it just stops. A watchdog-driven fail-safe
 * (forcing all 4 PWM outputs to full before halting) would be the more
 * thorough answer but needs care to get right from fault context — left
 * as a known gap, not implemented here. */
void HardFault_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

void MemManage_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

void BusFault_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

void UsageFault_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

void SysTick_Handler(void)
{
    g_tick_ms++;
}
