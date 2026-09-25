/*
 * wdg.c - independent watchdog, same as V2.89 (0x0800C642): reload 3125, IRC40K /64 -> about 5 s.
 */
#include "hal.h"

void WDG_Init(void)
{
#ifndef NO_WATCHDOG
    fwdgt_write_enable();
    fwdgt_config(3125U, FWDGT_PSC_DIV64);
    fwdgt_enable();
#endif
}
