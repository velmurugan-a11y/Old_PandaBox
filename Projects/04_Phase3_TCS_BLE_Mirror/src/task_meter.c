/*
 * task_meter.c -- Phase E of the FreeRTOS port (see the approved plan).
 * Moves the old bare-metal main() loop's meter/BLE logic into its own
 * task, done alone first (before Telit/OTA) since it owns the BLE
 * command path that already had a real-hardware incident this session --
 * the highest-risk single piece to migrate, so it gets migrated and
 * verified in isolation.
 */
#include "task_meter.h"
#include "board_config.h"
#include "tcs.h"
#include "yc1021.h"
#include "cmd.h"
#include "log.h"
#include "regs.h"

static void busy_wait(volatile uint32_t iterations)
{
    while (iterations--) { }
}

void vTaskMeter(void *pv)
{
    (void)pv;

#if BRINGUP_MODULE == BRINGUP_NONE
    tcs_init();
    log_line("\r\n--- TCS meter scan: trying node addresses 1-10 ---\r\n");
    for (uint8_t node = 1; node <= 10; node++) {
        uint16_t value = 0;
        int rc = tcs_scan_node(node, &value);
        if (rc == 1) {
            log_line("TCS: node ");
            log_uint(node);
            log_line(" responded, status value=");
            log_uint(value);
            log_line("\r\n");
        }
    }
    log_line("--- TCS scan done ---\r\n");
#endif

    yc1021_init();

    int printed_bt_sync = 0;

    for (;;) {
#if BRINGUP_MODULE == BRINGUP_NONE
        tcs_poll();
#endif
        yc1021_poll();

        if (cmd_reset_pending()) {
            /* Let the "LxBoxReset 0\r\n" reply actually finish shifting
             * out over UART before resetting -- matches the real
             * firmware's reply-then-100ms-delay-then-reset ordering. */
            busy_wait(30000000u); /* ~100ms at 120MHz (was 2000000 at the old 8MHz IRC, x15 for the Phase 2 clock switch) */
            SCB_AIRCR = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
            for (;;) { } /* unreachable: reset takes effect within a few cycles */
        }

        if (!printed_bt_sync && yc1021_is_synced()) {
            log_line("YC1021: link alive\r\n");
            printed_bt_sync = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(5)); /* replaces the old busy_wait(50) loop-rate limiter */
    }
}
