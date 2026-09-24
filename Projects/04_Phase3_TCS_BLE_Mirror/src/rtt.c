#include "rtt.h"
#include <stdint.h>
#include <string.h>

#define RTT_UP_BUF_SIZE 1024

typedef struct {
    const char *sName;
    char       *pBuffer;
    unsigned    SizeOfBuffer;
    unsigned    WrOff;
    unsigned    RdOff;
    unsigned    Flags;
} rtt_buf_t;

typedef struct {
    char      acID[16];
    int       MaxNumUpBuffers;
    int       MaxNumDownBuffers;
    rtt_buf_t aUp[1];
    rtt_buf_t aDown[1];
} rtt_cb_t;

static char rtt_up_buffer[RTT_UP_BUF_SIZE];

/* volatile + a distinct, non-.bss-zeroed section would be ideal so the
 * debugger can find it before .bss init runs, but this firmware's own
 * Reset_Handler zeroes .bss before main() anyway, and RTT is only read
 * after that point in practice -- plain .bss placement is fine here. */
static volatile rtt_cb_t _SEGGER_RTT = {
    .acID = "SEGGER RTT",
    .MaxNumUpBuffers = 1,
    .MaxNumDownBuffers = 0,
    .aUp = { { "Terminal", rtt_up_buffer, RTT_UP_BUF_SIZE, 0, 0, 0 } }, /* Flags=0: skip-on-full, never blocks */
};

void rtt_init(void)
{
    _SEGGER_RTT.aUp[0].WrOff = 0;
    _SEGGER_RTT.aUp[0].RdOff = 0;
}

void rtt_puts(const char *s)
{
    unsigned wr = _SEGGER_RTT.aUp[0].WrOff;
    unsigned size = _SEGGER_RTT.aUp[0].SizeOfBuffer;

    while (*s) {
        rtt_up_buffer[wr] = *s++;
        wr = (wr + 1 == size) ? 0 : wr + 1;
    }
    _SEGGER_RTT.aUp[0].WrOff = wr; /* single store, so a debugger read mid-write sees either the old or new offset, never a torn value */
}
