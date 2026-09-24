#include "log.h"
#include "uart.h"
#include "rtt.h"
#include "board_config.h"
#include "systick.h"
#include "cmd.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

static const uart_port_t debug_port = {
    .base    = DEBUG_UART_BASE,
    .tx_port = DEBUG_UART_GPIO_PORT, .tx_pin = DEBUG_UART_TX_PIN,
    .rx_port = DEBUG_UART_GPIO_PORT, .rx_pin = DEBUG_UART_RX_PIN,
};

/* Phase F of the FreeRTOS port (see the approved plan): uart_putc/
 * rtt_puts are confirmed non-thread-safe (unlocked read-modify-write of
 * the RTT ring buffer index, unlocked busy-poll on the UART TBE flag) --
 * with TaskA/Meter/Stats (and now Telit) all calling into this file from
 * separate tasks, an unprotected write here really can interleave mid-
 * line. Mutex guards the actual writes; log_line_locked() is the
 * mutex-free inner primitive so log_hex()/log_uint() can hold the mutex
 * across their own multi-call sequences without a recursive mutex. */
static StaticSemaphore_t s_log_mutex_buf;
static SemaphoreHandle_t s_log_mutex;

void log_init(void)
{
    rtt_init();
    /* AFIO->PCF0's debug-UART remap bit is applied inside clock_init();
     * this only configures pin mode + baud + enables. */
    uart_init(&debug_port, DEBUG_UART_BAUD, BOARD_PCLK2_HZ);
    s_log_mutex = xSemaphoreCreateMutexStatic(&s_log_mutex_buf);
}

static void log_line_locked(const char *s)
{
    rtt_puts(s);
    uart_puts(&debug_port, s);
}

void log_line(const char *s)
{
    /* s_log_mutex is NULL until log_init() runs (early in main(), before
     * the scheduler starts) -- a few log_line() calls happen before that
     * point (e.g. this project's own boot-order asserts never fire that
     * early in practice, but guard anyway rather than assume). Safe
     * unlocked in that window since nothing else is running yet. */
    if (s_log_mutex) { xSemaphoreTake(s_log_mutex, portMAX_DELAY); }
    log_line_locked(s);
    if (s_log_mutex) { xSemaphoreGive(s_log_mutex); }
}

static char hex_digit(uint8_t nibble)
{
    return (char)((nibble < 10) ? ('0' + nibble) : ('A' + (nibble - 10)));
}

void log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    char buf[3 * 20 + 1]; /* caps at 20 bytes per line; longer frames just truncate the printed tail */
    uint32_t n = (len > 20u) ? 20u : len;

    if (s_log_mutex) { xSemaphoreTake(s_log_mutex, portMAX_DELAY); }

    log_line_locked(prefix);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < n; i++) {
        buf[pos++] = hex_digit((uint8_t)(data[i] >> 4));
        buf[pos++] = hex_digit((uint8_t)(data[i] & 0x0Fu));
        buf[pos++] = ' ';
    }
    buf[pos] = '\0';
    log_line_locked(buf);
    log_line_locked("\r\n");

    if (s_log_mutex) { xSemaphoreGive(s_log_mutex); }
}

void log_uint(uint32_t v)
{
    char digits[10];
    char out[11];
    int  n = 0;

    if (v == 0u) {
        out[0] = '0';
        out[1] = '\0';
        log_line(out);
        return;
    }
    while (v > 0u) {
        digits[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (int i = 0; i < n; i++) {
        out[i] = digits[n - 1 - i];
    }
    out[n] = '\0';
    log_line(out);
}

void log_dbg_raw(char level, const char *file, int line, const char *msg)
{
    char buf[160];

    if (!cmd_debug_enabled()) {
        return;
    }
    snprintf(buf, sizeof(buf), "%c,%lu,%s,%d:%s\r\n",
             level, (unsigned long)millis(), file, line, msg);
    log_line(buf);
}
