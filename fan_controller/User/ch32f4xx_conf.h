/* Standard WCH/STM32-SPL-style peripheral library aggregator header —
 * pulls in whichever driver headers the vendored Libraries/Peripheral/inc
 * provides. Exact filenames depend on your EVT package version; adjust
 * this list to match what's actually in there if the build complains
 * about a missing header. */
#pragma once

#include "ch32f4xx_rcc.h"
#include "ch32f4xx_gpio.h"
#include "ch32f4xx_tim.h"
#include "ch32f4xx_adc.h"
#include "ch32f4xx_usart.h"
#include "ch32f4xx_misc.h"
