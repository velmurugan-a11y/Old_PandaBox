#ifndef RTT_H
#define RTT_H

/* Minimal SEGGER RTT (Real-Time Transfer) implementation -- lets J-Link
 * read log output over the SAME SWD connection used for flashing/debug,
 * no UART or USB needed. The debugger finds this by scanning target RAM
 * for the "SEGGER RTT" ID string, then reads/writes the buffer
 * descriptors that follow it -- a small, stable, publicly documented
 * protocol (not a proprietary link library). One up-channel only, in
 * non-blocking "skip on full" mode: if nobody's reading, old bytes just
 * get dropped rather than the firmware ever blocking on a debugger. */

void rtt_init(void);
void rtt_puts(const char *s);

#endif /* RTT_H */
