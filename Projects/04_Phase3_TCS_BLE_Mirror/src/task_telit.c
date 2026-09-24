/*
 * task_telit.c -- Phase F of the FreeRTOS port (see the approved plan).
 * Moves the EC25 4G modem's AT handshake into its own task. ec25.c's
 * poll function has no blocking calls, so it slots in unchanged; its
 * internal AT-retry timer WAS fixed from poll-count-based to tick-based
 * (see ec25.c) since a poll-count timer only meant what it said under
 * the old tight busy-wait loop.
 *
 * GPS, via the EC25's own AT+QGPS* commands (this board has no separate
 * GPS module, per the user's decision -- see the approved plan), is
 * folded in here once EC25 itself is confirmed alive.
 */
#include "task_telit.h"
#include "ec25.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"

void vTaskTelit(void *pv)
{
    (void)pv;

    ec25_init();

    int printed_ec25_ok = 0;
    int gps_started = 0;

    for (;;) {
        ec25_poll();

        if (!printed_ec25_ok && ec25_is_responding()) {
            log_line("EC25: AT -> OK -- modem alive\r\n");
            printed_ec25_ok = 1;
        }

        if (printed_ec25_ok && !gps_started) {
            ec25_gps_start();
            gps_started = 1;
        }
        if (gps_started) {
            ec25_gps_poll();
        }

        vTaskDelay(pdMS_TO_TICKS(20)); /* modem UART, less time-sensitive than the Meter task's BLE/TCS work */
    }
}
