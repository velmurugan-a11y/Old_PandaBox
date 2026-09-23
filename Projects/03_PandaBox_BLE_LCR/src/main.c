/*!
    \file    main.c
    \brief   PandaBox firmware: BLE (YC1021) <-> PandaBox protocol <-> two LCR meters (simulated, via LCP)

    Debug console on USART0 (PB6/PB7, 115200 8N1): logs every BLE command ("BLE<") and reply ("BLE>"),
    and accepts commands (type "help"). "inject <cmd>" runs a protocol command as if it came over BLE.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "uart.h"
#include "app.h"
#include "bt.h"
#include "lcr_host.h"
#include "lcp.h"
#include "meter.h"

extern meter_t *lcr_sim_meter(int port);

/* ------------------------------------------------------------------ time base */

static volatile uint32_t ms_tick;
static uint32_t epoch_base, epoch_base_ms;

void SysTick_Handler(void)
{
    ms_tick++;
}

uint32_t millis(void)
{
    return ms_tick;
}

void delay_ms(uint32_t ms)
{
    uint32_t s = ms_tick;
    while((ms_tick - s) < ms) {
    }
}

uint32_t app_time(void)
{
    return epoch_base + (ms_tick - epoch_base_ms) / 1000U;
}

void app_set_time(uint32_t epoch)
{
    epoch_base = epoch;
    epoch_base_ms = ms_tick;
}

static int month_of(const char *m)
{
    static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for(i = 0; i < 12; i++) {
        if(!strncmp(m, &names[i * 3], 3)) {
            return i + 1;
        }
    }
    return 1;
}

/* build date/time (__DATE__ "Sep 24 2026", __TIME__ "02:15:00") as the start time: the RTC has no
   coin cell on this board, and the app rejects timestamps before 2020 */
static uint32_t build_epoch(void)
{
    static const uint16_t mdays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const char *d = __DATE__, *t = __TIME__;
    int y = atoi(d + 7), mo = month_of(d), day = atoi(d + 4);
    uint32_t days = (uint32_t)(y - 1970) * 365U + (uint32_t)((y - 1969) / 4) + mdays[mo - 1] + (uint32_t)day - 1U;

    if(mo > 2 && (y % 4) == 0) {
        days++;
    }
    return days * 86400U + (uint32_t)atoi(t) * 3600U + (uint32_t)atoi(t + 3) * 60U + (uint32_t)atoi(t + 6);
}

const char *app_build_date(void)
{
    static char s[8];
    const char *d = __DATE__;
    sprintf(s, "%02d%02d%02d", atoi(d + 7) % 100, month_of(d), atoi(d + 4));
    return s;
}

/* ------------------------------------------------------------------ console */

int _write(int fd, const char *buf, int len)
{
    int i;
    (void)fd;
    for(i = 0; i < len; i++) {
        if(buf[i] == '\n') {
            uart_write(PORT_CON, (const uint8_t *)"\r", 1U);
        }
        uart_write(PORT_CON, (const uint8_t *)&buf[i], 1U);
    }
    return len;
}

static bt_link_t reply_link;

static void reply_ble(const char *line)
{
    printf("[%lu] BLE> %s\n", (unsigned long)millis(), line);
    bt_send_line(reply_link, line);
}

static void reply_console(const char *line)
{
    printf("REPLY %s\n", line);
}

void app_log_bt_event(const uint8_t *e, uint32_t n)
{
    uint32_t i;

    printf("[%lu] BT event", (unsigned long)millis());
    for(i = 0U; i < n; i++) {
        printf(" %02X", e[i]);
    }
    printf("\n");
}

static void show_status(void)
{
    int p;

    printf("time %lu, BT %s status %02X restarts %lu, rx %lu bytes, tx %lu frames, IMEI %s\n", (unsigned long)app_time(),
           bt_is_up() ? "up" : "DOWN", bt_status(), (unsigned long)bt_restarts(), (unsigned long)bt_rx_bytes(), (unsigned long)bt_tx_frames(), app_imei());
    for(p = 0; p < LCR_PORTS; p++) {
        lcr_port_t *lp = &lcr_port[p];
        meter_t *m = lcr_sim_meter(p);
        printf("port %d: node %u %s dev %02X ser %u | gross %ld flow %ld tot %ld prev %ld (tenths) | "
               "meter node %u state %02X preset %ld hist %u polls %lu/%lu\n",
               p + 1, lp->node, lp->online ? "online" : "OFFLINE", lp->dev_status, lp->serial,
               (long)lp->v[0], (long)lp->v[1], (long)lp->v[2], (long)lp->v[4],
               m->node, m->state, (long)m->preset, hist_count(p), (unsigned long)lp->polls_ok,
               (unsigned long)lp->polls_fail);
    }
}

static void console_cmd(char *line)
{
    char *arg = strchr(line, ' ');

    if(arg) {
        *arg++ = '\0';
    } else {
        arg = "";
    }
    if(!strcmp(line, "help")) {
        printf("status | inject <cmd> | flow <port> <tenths gal/min> | trace bt|lcp 0|1 | lcptest | "
               "btstart | reboot\n");
    } else if(!strcmp(line, "status")) {
        show_status();
    } else if(!strcmp(line, "inject")) {
        printf("INJECT %s\n", arg);
        proto_handle(arg, reply_console);
        printf("INJECT_END\n");
    } else if(!strcmp(line, "flow")) {
        int p = atoi(arg) - 1;
        char *v = strchr(arg, ' ');
        if(p >= 0 && p < LCR_PORTS && v) {
            lcr_sim_meter(p)->target_flow = atoi(v + 1);
            printf("port %d pump rate %d tenths gal/min\n", p + 1, atoi(v + 1));
        }
    } else if(!strcmp(line, "trace")) {
        int on = (strchr(arg, '1') != NULL);
        if(!strncmp(arg, "bt", 2)) {
            bt_trace = (uint8_t)on;
        } else if(!strncmp(arg, "lcp", 3)) {
            lcr_trace = (uint8_t)on;
        }
    } else if(!strcmp(line, "lcptest")) {
        int f = lcp_selftest();
        printf("RESULT lcptest %s failures=%d\n", f ? "FAIL" : "PASS", f);
    } else if(!strcmp(line, "btstart")) {
        bt_start(app_bt_name());
    } else if(!strcmp(line, "reboot")) {
        NVIC_SystemReset();
    } else if(line[0]) {
        printf("unknown '%s' (help)\n", line);
    }
}

/* ------------------------------------------------------------------ IMEI from the EC25 (background) */

static void modem_task(void)
{
    static enum { M_IDLE, M_BOOT, M_ASK, M_DONE } st = M_IDLE;
    static uint32_t t;
    static char l[40];
    static uint32_t n;
    int c;

    switch(st) {
    case M_IDLE:
        uart_init(PORT_GSM, 115200U);
        gpio_bit_set(GSM_PWR_PORT, GSM_PWR_PIN);
        delay_ms(300U);
        gpio_bit_set(GSM_PWRKEY_PORT, GSM_PWRKEY_PIN);
        delay_ms(600U);
        gpio_bit_reset(GSM_PWRKEY_PORT, GSM_PWRKEY_PIN);
        t = millis();
        st = M_BOOT;
        break;
    case M_BOOT:
    case M_ASK:
        while((c = uart_getc(PORT_GSM)) >= 0) {
            if(c == '\r' || c == '\n') {
                l[n] = '\0';
                if(n == 15U && strspn(l, "0123456789") == 15U) {
                    app_set_imei(l);
                    printf("modem: IMEI %s\n", l);
                    st = M_DONE;
                } else if(!strcmp(l, "RDY") && st == M_BOOT) {
                    st = M_ASK;
                    uart_puts(PORT_GSM, "ATE0\rAT+CGSN\r");
                }
                n = 0U;
            } else if(n < sizeof(l) - 1U) {
                l[n++] = (char)c;
            }
        }
        if(st == M_ASK && (millis() - t) > 25000U) {
            uart_puts(PORT_GSM, "AT+CGSN\r");
            t = millis();
        }
        if(st == M_BOOT && (millis() - t) > 25000U) {
            printf("modem: no RDY, IMEI unknown\n");
            st = M_DONE;
        }
        /* the modem stays powered (as in Leo's firmware): switching PE2 off again after the IMEI read
           left the BLE side unable to accept connections (bench, 2026-09-24) */
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    char line[160], con[160];
    uint32_t con_n = 0U, t_tick, t_poll, t_led;
    bt_link_t link;
    int c, p;

    board_init();
    SysTick_Config(SystemCoreClock / 1000U);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);
    uart_init(PORT_CON, 115200U);
    setvbuf(stdout, NULL, _IONBF, 0);
    app_set_time(build_epoch());
    gpio_bit_set(LED_PWR_PORT, LED_PWR_PIN);

    SystemCoreClockUpdate();
    printf("\n\n==== PandaBox BLE + LCR firmware %s (%s %s) ====\n", FW_SW_VER, __DATE__, __TIME__);
    printf("clock %lu Hz, LCP self-test %s\n", (unsigned long)SystemCoreClock, lcp_selftest() ? "FAIL" : "ok");

    proto_init();
    lcr_host_init();
    for(p = 0; p < LCR_PORTS; p++) {
        printf("port %d: meter node %u %s\n", p + 1, lcr_port[p].node, lcr_port[p].online ? "answered Product ID" : "no answer");
    }
    bt_start(app_bt_name());
    gpio_bit_set(LED_BT_PORT, LED_BT_PIN);
    printf("type 'help'\n> ");

    t_tick = t_poll = t_led = millis();
    while(1) {
        bt_poll();
        while(bt_get_line(line, sizeof(line), &link)) {
            printf("[%lu] BLE< %s\n", (unsigned long)millis(), line);
            reply_link = link;
            proto_handle(line, reply_ble);
        }
        /* meter physics every 100 ms, box poll every second (like Leo's 1 s poll) */
        if((millis() - t_tick) >= 100U) {
            t_tick += 100U;
            for(p = 0; p < LCR_PORTS; p++) {
                meter_tick(lcr_sim_meter(p), 100U);
            }
        }
        if((millis() - t_poll) >= 1000U) {
            t_poll += 1000U;
            lcr_host_poll();
        }
        /* LCP LEDs: on while the meter reports flow */
        if((millis() - t_led) >= 200U) {
            t_led = millis();
            gpio_bit_write(LED_LCP1_PORT, LED_LCP1_PIN, (lcr_port[0].online && lcr_port[0].v[1] > 0) ? SET : RESET);
            gpio_bit_write(LED_LCP2_PORT, LED_LCP2_PIN, (lcr_port[1].online && lcr_port[1].v[1] > 0) ? SET : RESET);
        }
        bt_service();
        modem_task();
        proto_tick();

        while((c = uart_getc(PORT_CON)) >= 0) {
            if(c == '\r' || c == '\n') {
                if(con_n) {
                    con[con_n] = '\0';
                    printf("\n");
                    console_cmd(con);
                    con_n = 0U;
                    printf("> ");
                }
            } else if((c == 0x08 || c == 0x7F) && con_n) {
                con_n--;
            } else if(c >= 0x20 && con_n < sizeof(con) - 1U) {
                con[con_n++] = (char)c;
                uart_write(PORT_CON, (const uint8_t *)&c, 1U);
            }
        }
    }
}
