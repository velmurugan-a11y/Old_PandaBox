/*
 * nvic.c -- NVIC priority-grouping setup (Phase B of the FreeRTOS port,
 * see the approved plan). Standard Cortex-M core registers (0xE000ED00
 * block), not vendor-specific.
 */
#include "nvic.h"
#include "regs.h"

void nvic_set_priority_grouping(void)
{
    /* AIRCR.PRIGROUP = 0: all implemented priority bits are group
     * (preemption) priority, no sub-priority. This is the setting every
     * Cortex-M FreeRTOS port assumes -- FreeRTOS itself only ever raises
     * BASEPRI to configMAX_SYSCALL_INTERRUPT_PRIORITY and never touches
     * sub-priority, so a nonzero PRIGROUP would silently change which
     * interrupts that masks.
     *
     * Deliberately a plain write, not read-modify-write: AIRCR's other
     * writable bit is SYSRESETREQ (bit 2) -- a read-modify-write that got
     * the mask even slightly wrong could accidentally set it and reset
     * the chip. A plain write with only VECTKEY set (PRIGROUP=0,
     * SYSRESETREQ=0, VECTCLRACTIVE=0) can't do that. */
    SCB_AIRCR = SCB_AIRCR_VECTKEY;
}
