/*
 * systick.h -- 1ms free-running tick counter (Cortex-M core SysTick
 * peripheral, not vendor-specific), used only for debug-log timestamps.
 */
#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/* core_hz: the actual core clock feeding SysTick (see board_config.h /
 * clock.c -- this Phase-1 build runs IRC8M with no PLL, so 8000000). */
void systick_init(uint32_t core_hz);

/* Milliseconds since systick_init(), free-running (wraps at ~49.7 days). */
uint32_t millis(void);

#endif
