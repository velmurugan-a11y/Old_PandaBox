/*
 * systick.c -- 1ms tick, POLLED not interrupt-driven. Standard Cortex-M
 * core peripheral registers (0xE000E010), same on every Cortex-M3/M4
 * part -- not GD32-specific.
 *
 * Deliberately does NOT enable SysTick's interrupt (TICKINT): this whole
 * project is bare-metal/polling (no other interrupts in use either, see
 * startup_gd32f305vct6.s), so adding the one exception vector just for a
 * debug-log timestamp isn't worth it. Polling COUNTFLAG (auto-clears on
 * read, set once per underflow) from millis() gives the same practical
 * result as long as millis() is called reasonably often, which it is --
 * every dispatch_command() and raw_rx_flush() call.
 *
 * (An earlier version of this file DID enable TICKINT, and a test run
 * right after showed genuinely corrupted debug-UART output -- but that
 * was traced to multiple stale JLinkRTTLogger processes left running
 * from earlier tests, all contending for the same SWD connection, not
 * the interrupt itself. Kept polling-only anyway since there's no real
 * need for the interrupt here.)
 */
#include "systick.h"

#define SYST_CSR   (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR   (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR   (*(volatile uint32_t *)0xE000E018u)

#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2) /* 1 = core clock, 0 = core/8 */
#define SYST_CSR_COUNTFLAG (1u << 16)

static volatile uint32_t s_ms_ticks;

void systick_init(uint32_t core_hz)
{
    SYST_RVR = (core_hz / 1000u) - 1u;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE; /* no TICKINT */
}

uint32_t millis(void)
{
    if ((SYST_CSR & SYST_CSR_COUNTFLAG) != 0u) {
        s_ms_ticks++; /* reading CSR above already cleared COUNTFLAG */
    }
    return s_ms_ticks;
}
