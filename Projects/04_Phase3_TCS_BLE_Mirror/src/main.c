/*
 * main.c -- Phase 1 bring-up: clocks, LED test, dual RTT+UART logging,
 * YC1021 HCI-H5 sync, and EC25 AT handshake. See board_config.h for
 * every board-specific assumption this relies on and its confidence
 * level.
 *
 * Logging (see log.h) goes to BOTH SEGGER RTT (read over the same SWD
 * connection used to flash -- no extra hardware needed) and the debug
 * UART (DB9/J3, once you have a proper RS232-capable adapter for it).
 */
#include <stdint.h>
#include <stdio.h>
#include "clock.h"
#include "gpio.h"
#include "log.h"
#include "cmd_selftest.h"
#include "flash_store.h"
#include "regs.h"
#include "systick.h"
#include "nvic.h"
#include "board_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_meter.h"
#include "task_telit.h"
#include "task_ota.h"

static void busy_wait(volatile uint32_t iterations)
{
    while (iterations--) { }
}

typedef struct {
    gpio_reg_t *port;
    uint8_t     pin;
    const char *name;
} led_t;

static const led_t leds[] = {
    { LED_GPS_GREEN_PORT,   LED_GPS_GREEN_PIN,   "GPS green   (PB0)" },
    { LED_PWR_RED_PORT,     LED_PWR_RED_PIN,     "PWR red     (PB1)" },
    { LED_WIFI_ORANGE_PORT, LED_WIFI_ORANGE_PIN, "WiFi orange (PC4)" },
    { LED_BT_BLUE_PORT,     LED_BT_BLUE_PIN,     "BT blue     (PC5)" },
    { LED_LCP1_PORT,        LED_LCP1_PIN,        "LCP1        (PC6)" },
    { LED_LCP2_PORT,        LED_LCP2_PIN,        "LCP2        (PC7)" },
};
#define NUM_LEDS (sizeof(leds) / sizeof(leds[0]))

#if BRINGUP_MODULE == BRINGUP_NONE
static void led_test_sequence(void)
{
    log_line("\r\n--- LED test: each LED on/off in turn, 3 cycles ---\r\n");
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        for (unsigned i = 0; i < NUM_LEDS; i++) {
            log_line("LED ON:  ");
            log_line(leds[i].name);
            log_line("\r\n");
            gpio_write(leds[i].port, leds[i].pin, 1);
            busy_wait(6000000u); /* x15 for the Phase 2 clock switch, was 400000 at 8MHz */
            gpio_write(leds[i].port, leds[i].pin, 0);
            busy_wait(1500000u); /* x15, was 100000 at 8MHz */
        }
    }
    log_line("--- LED test done ---\r\n");
}
#endif /* BRINGUP_MODULE == BRINGUP_NONE */

/* ------------------------------------------------------------------ *
 * Phase C smoke test: two trivial, independent tasks proving preemptive
 * scheduling + tick + context switch all work on real hardware, BEFORE
 * any real peripheral module (yc1021/ec25/tcs/cmd) is migrated into a
 * task (that's Phase E/F). Different LEDs, different rates, independent
 * RTT counters -- see the approved FreeRTOS-port plan.
 * ------------------------------------------------------------------ */
/* 128 words was the original guess for these "trivial" dummy tasks --
 * Phase D's stats task caught it as genuinely too tight in practice
 * (TaskA/TaskB down to 8-9 words free, Stats itself fully overflowed)
 * once snprintf's real stack appetite (used for the RTT log messages)
 * is accounted for. Bumped generously based on that real measurement
 * rather than guessing again. */
#define DUMMY_STACK_WORDS 256u

#if BRINGUP_MODULE == BRINGUP_NONE
static StaticTask_t s_taskA_tcb;
static StackType_t  s_taskA_stack[DUMMY_STACK_WORDS];

/* Kept as an ongoing "is the scheduler still healthy" heartbeat now that
 * Phase E gives the Meter task real work -- TaskB (its Phase C partner)
 * is retired, its job done. */
static void vTaskA(void *pv)
{
    (void)pv;
    uint32_t count = 0;
    char msg[48];
    for (;;) {
        gpio_toggle(LED_PWR_RED_PORT, LED_PWR_RED_PIN);
        snprintf(msg, sizeof(msg), "TaskA (PWR red, 500ms): %lu\r\n", (unsigned long)count++);
        log_line(msg);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif /* BRINGUP_MODULE == BRINGUP_NONE */

/* ------------------------------------------------------------------ *
 * Phase D: stack high-water-mark stats task -- the ongoing tool for
 * right-sizing real tasks' stacks later (Phase E/F) instead of guessing,
 * plus a one-shot deliberate stack-overflow drill proving
 * vApplicationStackOverflowHook (freertos/freertos_hooks.c) actually
 * fires and logs cleanly before it's ever needed for real. See the
 * approved plan.
 * ------------------------------------------------------------------ */
#if BRINGUP_MODULE == BRINGUP_NONE
static TaskHandle_t s_hTaskA;
static TaskHandle_t s_hStats;
#endif
#if BRINGUP_MODULE != BRINGUP_ONLY_TELIT
static TaskHandle_t s_hMeter;
#endif
#if BRINGUP_MODULE != BRINGUP_ONLY_BLE
static TaskHandle_t s_hTelit;
#endif

#if BRINGUP_MODULE == BRINGUP_NONE
#define STATS_STACK_WORDS 256u /* see DUMMY_STACK_WORDS note -- 128 overflowed here for real */
static StaticTask_t s_taskStats_tcb;
static StackType_t  s_taskStats_stack[STATS_STACK_WORDS];
#endif

#if BRINGUP_MODULE != BRINGUP_ONLY_TELIT
/* Meter task (Phase E): 1024 words (4096 bytes), matching the reference
 * architecture document's own Meter-task sizing -- unlike the dummy
 * tasks' stacks, this isn't a first guess to be measured and fixed
 * later; it's carried over from a real, if different-MCU, product's
 * already-tuned value. Still watched via the stats task below in case
 * this MCU's real usage (different compiler, different call depths)
 * differs enough to matter. */
#define METER_STACK_WORDS 1024u
static StaticTask_t s_taskMeter_tcb;
static StackType_t  s_taskMeter_stack[METER_STACK_WORDS];
#endif

#if BRINGUP_MODULE != BRINGUP_ONLY_BLE
/* Telit task (Phase F, GPS sub-step): reference doc's Telit (4096B) +
 * GPS (2048B) combined sizing, now that GPS AT handling is folded in
 * here per the plan. Still watched via the Stats task in case actual
 * usage on this MCU differs. */
#define TELIT_STACK_WORDS 1536u
static StaticTask_t s_taskTelit_tcb;
static StackType_t  s_taskTelit_stack[TELIT_STACK_WORDS];
#endif

#if BRINGUP_MODULE == BRINGUP_NONE
/* OTA task (Phase G): true placeholder, deliberately minimal -- see
 * task_ota.c. */
#define OTA_TASK_STACK_WORDS 128u
static StaticTask_t s_taskOta_tcb;
static StackType_t  s_taskOta_stack[OTA_TASK_STACK_WORDS];
static TaskHandle_t s_hOta;
#endif /* BRINGUP_MODULE == BRINGUP_NONE */

#ifndef PHASE_D_OVERFLOW_DRILL
#define PHASE_D_OVERFLOW_DRILL 0
#endif

#if PHASE_D_OVERFLOW_DRILL
/* Deliberately tiny -- just enough to prove the overflow hook fires, not
 * enough to run vTaskA's normal body. Recursion below is what overflows
 * it on purpose. */
#define OVERFLOW_STACK_WORDS 40u
static StaticTask_t s_taskOverflow_tcb;
static StackType_t  s_taskOverflow_stack[OVERFLOW_STACK_WORDS];

static void vTaskDeliberateOverflow(void *pv)
{
    /* A SINGLE bounded write well past the stack's end, then an
     * immediate yield -- not unbounded recursion. Recursion (tried
     * first) kept consuming more stack every iteration for as long as it
     * ran before the next scheduler tick's check, at 8MHz that's enough
     * iterations to corrupt memory far beyond the intended tiny stack --
     * badly enough, on real hardware, to corrupt the RTT control block
     * itself and make the target undiscoverable by the debug probe. A
     * single write is bounded damage; the immediate vTaskDelay(1) forces
     * a context switch right away so the checker gets its very next
     * chance to catch it before any more damage happens. */
    (void)pv;
    log_line("\r\nPhase D: deliberately overflowing a tiny task stack now (expect the overflow hook to fire and halt) -- ");
    {
        volatile uint32_t overflow_buf[100]; /* 400 bytes, well past OVERFLOW_STACK_WORDS' 160 bytes */
        for (uint32_t i = 0; i < 100u; i++) {
            overflow_buf[i] = i;
        }
    }
    vTaskDelay(1);
    log_line("SHOULD NOT REACH HERE -- overflow hook did not fire\r\n");
    for (;;) { vTaskDelay(1000); }
}
#endif /* PHASE_D_OVERFLOW_DRILL */

#if BRINGUP_MODULE == BRINGUP_NONE
static void vTaskStats(void *pv)
{
    (void)pv;
    char msg[96];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        snprintf(msg, sizeof(msg),
            "STATS (words free): TaskA=%u Meter=%u Telit=%u OTA=%u Stats=%u\r\n",
            (unsigned)uxTaskGetStackHighWaterMark(s_hTaskA),
            (unsigned)uxTaskGetStackHighWaterMark(s_hMeter),
            (unsigned)uxTaskGetStackHighWaterMark(s_hTelit),
            (unsigned)uxTaskGetStackHighWaterMark(s_hOta),
            (unsigned)uxTaskGetStackHighWaterMark(s_hStats));
        log_line(msg);
    }
}
#endif /* BRINGUP_MODULE == BRINGUP_NONE */

int main(void)
{
    /* clock_init() MUST run first -- it's what enables GPIOB/C's
     * peripheral clocks in the first place. A GPIO register write before
     * its port's RCU clock bit is set is a no-op on this family. */
    clock_init();
    nvic_set_priority_grouping(); /* explicit, not relying on silicon reset default -- see nvic.c */
    systick_init(BOARD_SYSCLK_HZ);

    /* Still first thing after clocks are up -- if this board's power-latch
     * circuit needs PC8 held high to stay powered, every microsecond
     * before this matters. */
    gpio_set_output_high(PWRHOLD_PORT, PWRHOLD_PIN);

    /* WDI0/WDI1 (PD0/PD1) deliberately NOT configured here -- see the
     * conflict note in board_config.h. Those pins also serve as this
     * board's HXTAL crystal input/output (X101 is populated there); do
     * not add GPIO config for them without resolving that first. */

    for (unsigned i = 0; i < NUM_LEDS; i++) {
        gpio_set_output_low(leds[i].port, leds[i].pin);
    }

    log_init();
    log_line("\r\n--- PandaBox GD32F305VCT6 native bring-up ---\r\n");
    log_line(clock_is_pll()
        ? "clocks: HXTAL(12MHz)+PLL x10 -> 120MHz SYSCLK (Phase 2)\r\n"
        : "clocks: *** FELL BACK TO IRC8M (8MHz) -- HXTAL/PLL never stabilized *** \r\n");
    log_line("log path: RTT (over SWD) + debug UART (DB9/J3, PB6/PB7)\r\n");

    /* Grace period so an RTT logger has time to attach before the (very
     * fast, no inter-command delay) self-test burst below -- otherwise
     * it's over before most debugger tools finish connecting. */
    log_line("cmd self-test starts in ~3s...\r\n");
    busy_wait(90000000u); /* ~3s at 120MHz (was 6000000 at the old 8MHz IRC, x15 for the Phase 2 clock switch) */

    /* Phase 2 (flash storage) bring-up gate: JEDEC ID + erase/write/read
     * round trip, BEFORE anything else touches the flash. Purely
     * informational at this stage -- flash_store isn't wired into
     * cmd.c's real command handlers yet (see the port plan's Phase 2
     * verification gate); this just proves the SPI0 pin guess in
     * board_config.h right or wrong on whatever hardware this boots on. */
    flash_store_init();
    if (!flash_store_selftest()) {
        log_line("flash_store: NOT wired into cmd.c yet -- pending pin fix, see board_config.h\r\n");
    }

    cmd_selftest_run(); /* run first, before LED/TCS delays push it out of the small RTT ring buffer */

#if BRINGUP_MODULE == BRINGUP_NONE
    led_test_sequence();
#endif

    /* Phase E of the FreeRTOS port (see the approved plan): the TCS scan
     * and YC1021/cmd.c init+poll loop that used to run right here in
     * main() now live in task_meter.c's vTaskMeter, given the Meter
     * priority (3) above the housekeeping tasks (1) -- it owns the BLE
     * command path that already had a real-hardware incident this
     * session, so it's migrated and verified alone before Telit/OTA. */
    log_line("\r\n--- FreeRTOS: starting scheduler ---\r\n");

#if BRINGUP_MODULE == BRINGUP_ONLY_BLE
    s_hMeter = xTaskCreateStatic(vTaskMeter, "Meter", METER_STACK_WORDS, NULL, 3, s_taskMeter_stack, &s_taskMeter_tcb);
    log_line(s_hMeter ? "Meter task created OK (BLE-only bring-up -- TCS/LED/Telit/OTA/Stats all skipped)\r\n" : "TASK CREATE FAILED\r\n");
#elif BRINGUP_MODULE == BRINGUP_ONLY_TELIT
    s_hTelit = xTaskCreateStatic(vTaskTelit, "Telit", TELIT_STACK_WORDS, NULL, 2, s_taskTelit_stack, &s_taskTelit_tcb);
    log_line(s_hTelit ? "Telit task created OK (4G-only bring-up -- Meter/TCS/LED/OTA/Stats all skipped)\r\n" : "TASK CREATE FAILED\r\n");
#else
    s_hTaskA = xTaskCreateStatic(vTaskA, "TaskA", DUMMY_STACK_WORDS, NULL, 1, s_taskA_stack, &s_taskA_tcb);
    s_hMeter = xTaskCreateStatic(vTaskMeter, "Meter", METER_STACK_WORDS, NULL, 3, s_taskMeter_stack, &s_taskMeter_tcb);
    s_hTelit = xTaskCreateStatic(vTaskTelit, "Telit", TELIT_STACK_WORDS, NULL, 2, s_taskTelit_stack, &s_taskTelit_tcb);
    s_hOta   = xTaskCreateStatic(vTaskOta, "OTA", OTA_TASK_STACK_WORDS, NULL, 1, s_taskOta_stack, &s_taskOta_tcb);
    s_hStats = xTaskCreateStatic(vTaskStats, "Stats", STATS_STACK_WORDS, NULL, 1, s_taskStats_stack, &s_taskStats_tcb);
    log_line((s_hTaskA && s_hMeter && s_hTelit && s_hOta && s_hStats) ? "Tasks created OK\r\n" : "TASK CREATE FAILED\r\n");
#endif

#if PHASE_D_OVERFLOW_DRILL
    /* One-shot hardening drill (see the approved plan) -- proves
     * vApplicationStackOverflowHook actually fires before it's ever
     * needed for real. Deliberately halts the whole system once it
     * fires (the hook spins forever) -- build with
     * -DPHASE_D_OVERFLOW_DRILL=1 for this one verification flash only,
     * then rebuild without it for normal operation. */
    xTaskCreateStatic(vTaskDeliberateOverflow, "Overflow", OVERFLOW_STACK_WORDS, NULL, 1,
                       s_taskOverflow_stack, &s_taskOverflow_tcb);
#endif

    vTaskStartScheduler();

    /* Should never reach here -- vTaskStartScheduler() only returns on
     * failure (e.g. out of heap for the idle task under dynamic
     * allocation; shouldn't happen here since the idle task is statically
     * allocated via vApplicationGetIdleTaskMemory). */
    log_line("FATAL: vTaskStartScheduler() returned\r\n");
    for (;;) { }
}
