/*
 * ec25.c -- Quectel EC25 (exact populated variant: EC25AFA-512-STD) power-
 * on + basic "AT" handshake bring-up.
 *
 * PWRKEY polarity assumption, UNCONFIRMED: the schematic shows PB15_GSM_
 * PWRKEY driving PWRKEY through a transistor stage (Q4 + R47/R48), not a
 * direct GPIO-to-pin connection. This code assumes the common topology --
 * an NPN pulling PWRKEY to GND when its base is driven -- so it drives
 * PB15 HIGH to assert the (active-low, per the EC25 pinout) PWRKEY pulse.
 * If the modem never responds to "AT", the first thing to check on a
 * scope/logic analyzer is whether that assumption has the polarity
 * backwards, not that the UART wiring is wrong.
 *
 * Pulse width (600ms) is a commonly-cited conservative default for
 * Quectel EC2x-series PWRKEY, NOT copied from EC25AFA-512-STD's own
 * hardware design guide (not available in this pass) -- verify against
 * that document before relying on this exact timing in production.
 */
#include "ec25.h"
#include "uart.h"
#include "gpio.h"
#include "board_config.h"
#include "log.h"
#include "cmd.h"
#include "regs.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static const uart_port_t s_port = {
    .base    = EC25_UART_BASE,
    .tx_port = EC25_GPIO_PORT, .tx_pin = EC25_TX_PIN,
    .rx_port = EC25_RX_PORT,   .rx_pin = EC25_RX_PIN,
};

/* Real bug fix, found by comparing against a proven-working reference
 * driver on the SAME physical board (docs/PandaBox_Peripheral_Bringup
 * bring-up rig): that driver is interrupt-driven with a ring buffer;
 * this one was bare-polling a GD32/STM32-style USART, which has only a
 * ONE-BYTE hardware receive register and no FIFO. ec25_poll() is only
 * called once per FreeRTOS tick (5ms) from vTaskTelit's loop -- any real
 * multi-byte reply (RDY, OK, ATI text, ...) arriving as a fast UART
 * burst between two poll calls got silently collapsed down to whatever
 * single byte happened to still be sitting in DATA the next time we
 * checked, which is exactly the "always exactly one byte" symptom seen
 * on real hardware. Moving RX into an ISR + ring buffer means no byte is
 * ever lost to scheduling gaps, matching the reference driver's design. */
#define EC25_RX_RING_SIZE 256u
static volatile uint8_t  s_rx_ring[EC25_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint32_t s_rx_overruns;

void UART4_IRQHandler(void)
{
    if ((EC25_UART_BASE->STAT & (USART_STAT_RBNE | USART_STAT_ORERR)) != 0) {
        /* Reading STAT then DATA clears both RBNE and ORERR -- STAT was
         * already read by the condition above, so this DATA read is what
         * actually clears the flags. */
        uint8_t b = (uint8_t)EC25_UART_BASE->DATA;
        uint32_t next = (s_rx_head + 1u) % EC25_RX_RING_SIZE;
        if (next != s_rx_tail) {
            s_rx_ring[s_rx_head] = b;
            s_rx_head = next;
        } else {
            s_rx_overruns++; /* ring full -- caller isn't draining fast enough */
        }
    }
}

static int ec25_rx_ready(void)
{
    return s_rx_head != s_rx_tail;
}

static uint8_t ec25_rx_getc(void)
{
    uint8_t b = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1u) % EC25_RX_RING_SIZE;
    return b;
}

static char     rx_line[64];
static uint32_t rx_len;
static int      responded;

/* BUG FIX (Phase F of the FreeRTOS port -- see the approved plan): this
 * used to be a poll-COUNT-based timer (retry every AT_RETRY_POLLS calls
 * to ec25_poll()), which only worked because the old bare-metal main()
 * loop called ec25_poll() in a tight busy-wait with no real delay. Now
 * that ec25_poll() is called from a vTaskDelay-paced task loop (Phase F),
 * a poll-count-based timer at the old count would stretch the retry
 * interval from a few seconds to tens of minutes. Switched to a real
 * tick-based timer so the retry interval means what it says regardless
 * of how often the caller happens to poll. */
#define AT_RETRY_MS 2000u
static TickType_t last_at_send_tick;

static void busy_wait(volatile uint32_t iterations)
{
    while (iterations--) { }
}

void ec25_init(void)
{
    /* Real bug fix, confirmed via the PandaBox master hardware note:
     * the documented power-on sequence is "PE2=1, wait for the supply
     * to settle, THEN pulse PWRKEY" -- PE2 (VGSM LDO enable) was never
     * defined or driven anywhere in this codebase before, meaning the
     * modem has been genuinely unpowered through every previous AT/
     * PWRKEY attempt this session, regardless of protocol correctness.
     * VGSM's output voltage was confirmed safe (~4.0V, not the
     * documented-damaging 5.3V case) before enabling this. */
    log_line("\r\n--- EC25: enabling VGSM (PE2) ---\r\n");
    gpio_set_output_high(EC25_POWER_PORT, EC25_POWER_PIN);
    vTaskDelay(pdMS_TO_TICKS(100)); /* let the supply settle before pulsing PWRKEY, per the documented sequence */

    log_line("EC25: pulsing PWRKEY\r\n");
    gpio_set_output_low(EC25_PWRKEY_PORT, EC25_PWRKEY_PIN);
    gpio_set_output_low(EC25_DTR_PORT, EC25_DTR_PIN); /* DTR low = modem stays out of sleep --
        confirmed this isn't the cause of the "stuck single byte" RX pattern (tested undriven, no
        change) -- see ec25.c's git history / session notes if that needs re-checking */

    gpio_write(EC25_PWRKEY_PORT, EC25_PWRKEY_PIN, 1); /* assert pulse -- see polarity note above */
    busy_wait(9000000u); /* ~600ms at 120MHz (was 600000 at the old 8MHz IRC, x15 for the Phase 2 clock switch) --
                           * meets the master hardware note's documented "PB15 = 1 for >= 500ms" requirement */
    gpio_write(EC25_PWRKEY_PORT, EC25_PWRKEY_PIN, 0);
    log_line("EC25: PWRKEY pulse done, UART starting\r\n");

    uart_init(&s_port, EC25_UART_BAUD, BOARD_PCLK1_HZ);

    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_overruns = 0;
    EC25_UART_BASE->CTL0 |= USART_CTL0_RBNEIE; /* uart_init() set UEN/TEN/REN only */
    nvic_enable_irq(UART4_IRQN);

    /* Real bug fix, confirmed via the real Quectel EC25 Series Hardware
     * Design doc's own "Power-up Timing" diagram (Figure 12): PWRKEY only
     * needs to be held low >=500ms, but the module's UART is documented as
     * staying INACTIVE for >=12s after VBAT goes stable (EC25 boots an
     * internal Linux/baseband on NAND+DDR2 -- this is a real multi-second
     * boot, not just a power sequencing formality). We were sending "AT"
     * within ~1s of enabling VGSM, many seconds before the modem's own
     * UART engine is guaranteed to be listening -- a very plausible cause
     * of the "single 0x0D then repeating 0x41" garbage seen on real
     * hardware (catching the modem mid-boot rather than a real reply). */
    log_line("EC25: waiting ~13s for UART to become active per datasheet power-up timing\r\n");
    vTaskDelay(pdMS_TO_TICKS(13000));

    rx_len = 0;
    responded = 0;
    last_at_send_tick = xTaskGetTickCount() - pdMS_TO_TICKS(AT_RETRY_MS); /* send the first "AT" immediately once the boot-settle wait above is done */
}

static void handle_line(const char *line)
{
    log_line("[GSM RX] ");
    log_line(line);
    log_line("\r\n");
    if (strstr(line, "OK") != NULL) {
        responded = 1;
    }
}

/* Raw RX tap -- logs every byte as it actually arrives, independent of
 * line framing, so partial/garbled responses are visible too (same
 * diagnostic pattern as yc1021.c's raw_rx_tap). */
static uint8_t  raw_buf[16];
static uint32_t raw_len;

static void raw_rx_flush(void)
{
    if (raw_len > 0u) {
        if (cmd_debug_enabled()) {
            log_hex("[GSM DBG] RAW RX: ", raw_buf, raw_len);
        }
        raw_len = 0u;
    }
}

static void raw_rx_tap(uint8_t b)
{
    raw_buf[raw_len++] = b;
    if (raw_len >= sizeof(raw_buf)) {
        raw_rx_flush();
    }
}

void ec25_poll(void)
{
    int got_any = 0;
    while (ec25_rx_ready()) {
        uint8_t b = ec25_rx_getc();
        raw_rx_tap(b);
        got_any = 1;
        if (b == '\n' || b == '\r') {
            if (rx_len > 0) {
                rx_line[rx_len] = '\0';
                handle_line(rx_line);
                rx_len = 0;
            }
        } else if (rx_len < sizeof(rx_line) - 1) {
            rx_line[rx_len++] = (char)b;
        }
    }
    if (got_any) {
        raw_rx_flush();
    }

    if (!responded) {
        if ((xTaskGetTickCount() - last_at_send_tick) >= pdMS_TO_TICKS(AT_RETRY_MS)) {
            log_line("[GSM TX] AT\r\n");
            uart_puts(&s_port, "AT\r\n");
            last_at_send_tick = xTaskGetTickCount();
        }
    }
}

int ec25_is_responding(void)
{
    return responded;
}

/* ------------------------------------------------------------------ *
 * GNSS -- this board has no separate GPS module; GPS is serviced via
 * the EC25's own AT+QGPS* command set (Quectel EC25&EC21 GNSS AT
 * Commands Manual V1.1, confirmed this session), sharing the same UART
 * as the cellular AT commands above (both are just AT commands to the
 * same modem). Deliberately the "later sub-step" the FreeRTOS-port plan
 * called out for Phase F: stand-alone mode, default accuracy/timeout,
 * continuous fix (<fixcount>=0), polled via AT+QGPSLOC? every few
 * seconds rather than parsing raw NMEA off a second port.
 *
 * Blocking send/wait pattern -- this only runs
 * inside vTaskTelit's own task context (never called from an ISR or
 * from another task), so blocking here just delays Telit's own next
 * loop iteration, not the rest of the system. ec25_poll() above stops
 * sending anything once responded=1, so once EC25 is confirmed alive,
 * it becomes a passive listener and doesn't collide with these blocking
 * exchanges on the same UART.
 * ------------------------------------------------------------------ */
#define GPS_LINE_MAX 96

static int gps_at_send_and_wait(const char *cmd, uint32_t timeout_ms)
{
    char line[GPS_LINE_MAX];
    uint32_t line_len = 0;
    TickType_t start = xTaskGetTickCount();

    uart_puts(&s_port, cmd);
    uart_puts(&s_port, "\r\n");
    log_line("[GSM TX] ");
    log_line(cmd);
    log_line("\r\n");

    for (;;) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            log_line("EC25 GPS: (timeout waiting for response)\r\n");
            return 0;
        }
        if (!ec25_rx_ready()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        uint8_t b = ec25_rx_getc();
        if (b == '\n') {
            continue;
        }
        if (b == '\r') {
            if (line_len == 0u) {
                continue;
            }
            line[line_len] = '\0';
            log_line("[GSM RX] ");
            log_line(line);
            log_line("\r\n");

            if (strcmp(line, "OK") == 0) {
                return 1;
            }
            if (strcmp(line, "ERROR") == 0 || strncmp(line, "+CME ERROR", 10) == 0) {
                return 0;
            }
            line_len = 0; /* an information line (e.g. +QGPSLOC: ...) -- already logged, keep waiting for OK/ERROR */
        } else if (line_len < GPS_LINE_MAX - 1u) {
            line[line_len++] = (char)b;
        }
    }
}

static int      gps_started;
#define GPS_POLL_INTERVAL_MS 5000u
static TickType_t last_gps_poll_tick;

void ec25_gps_start(void)
{
    log_line("\r\n--- EC25 GNSS bring-up: AT+QGPS=1 ---\r\n");
    /* Default gnssmode(1=stand-alone)/fixmaxtime(30s)/fixmaxdist(50m)/
     * fixcount(0=continuous) -- matches the manual's own basic example. */
    if (gps_at_send_and_wait("AT+QGPS=1", 3000)) {
        log_line("EC25 GNSS: ON\r\n");
    } else {
        log_line("EC25 GNSS: AT+QGPS=1 did not return OK (may already be on from a prior session, or GNSS locked/busy) -- will still try AT+QGPSLOC? below\r\n");
    }
    gps_started = 1;
    last_gps_poll_tick = xTaskGetTickCount() - pdMS_TO_TICKS(GPS_POLL_INTERVAL_MS); /* poll location immediately, not after one full interval */
}

void ec25_gps_poll(void)
{
    if (!gps_started) {
        return;
    }
    if ((xTaskGetTickCount() - last_gps_poll_tick) < pdMS_TO_TICKS(GPS_POLL_INTERVAL_MS)) {
        return;
    }
    last_gps_poll_tick = xTaskGetTickCount();

    /* Response is +QGPSLOC: <UTC>,<lat>,<lon>,<hdop>,<alt>,<fix>,<cog>,
     * <spkm>,<spkn>,<date>,<nsat> on success, or +CME ERROR: 516 ("Not
     * fixed now") before the engine has a fix yet -- both cases are
     * just logged as-is by gps_at_send_and_wait, no parsing needed for
     * this bring-up milestone. */
    gps_at_send_and_wait("AT+QGPSLOC?", 2000);
}
