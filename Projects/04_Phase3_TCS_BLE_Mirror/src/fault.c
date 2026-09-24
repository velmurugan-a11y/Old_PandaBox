/*
 * fault.c -- real HardFault handler so a crash is visible over RTT/debug
 * UART instead of silently hanging in Default_Handler's infinite loop.
 * Added directly because of a real incident: an early FreeRTOS
 * xPortStartScheduler() bug (a VTOR read that didn't apply to this MCU)
 * caused exactly this kind of silent hang, with zero diagnostic output,
 * making it much harder to root-cause than it needed to be. Kept
 * deliberately simple (plain C, no stack-frame unwinding) -- presence of
 * *any* log output beats a fancier handler that itself might not run
 * reliably from fault context.
 */
#include "log.h"

void HardFault_Handler(void)
{
    log_line("\r\n*** HARD FAULT -- CPU halted ***\r\n");
    for (;;) { }
}
