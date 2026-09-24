#ifndef EC25_H
#define EC25_H

void ec25_init(void);       /* pulses PWRKEY, brings up the UART */
void ec25_poll(void);       /* call repeatedly from the main loop */
int  ec25_is_responding(void); /* 1 once "OK" has been seen for a plain "AT" */

/* GNSS, via the EC25's own AT+QGPS* commands -- see ec25.c. Call
 * ec25_gps_start() once, after ec25_is_responding() is confirmed; then
 * call ec25_gps_poll() every task loop iteration (it self-paces to
 * query AT+QGPSLOC? every few seconds). */
void ec25_gps_start(void);
void ec25_gps_poll(void);

#endif /* EC25_H */
