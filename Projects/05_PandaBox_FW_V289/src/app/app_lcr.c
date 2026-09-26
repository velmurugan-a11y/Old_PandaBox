/*
 * app_lcr.c - LCR side of the box: two ports over LCP (fields #2/#4/#17/#18/#100/#101 polled once a
 * second, records stored on change) and the full App command parser with V2.89 reply strings.
 *
 * The LCP framing/CRC (lcp.c), the meter simulator (meter.c), the port/poll/history engine
 * (lcr_host.c) and the command dispatcher (app_lcr_proto.c) are the modules proven against the
 * pandabox-tester in Projects/03; this file drives them from the V2.89 super-loop and routes replies
 * back through the current send mode (BT / WiFi / 4G), like Leo's applcrSendDataToAPP.
 *
 * To move from the simulator to real meters, replace lcr_transport() in lcr_host.c with a UART
 * exchange on USART1/USART2 at 19200 with the RS232/RS485 direction handling.
 */
#include <string.h>
#include "app.h"
#include "drv.h"
#include "app_compat.h"
#include "lcr_host.h"
#include "lcp.h"

static uint8_t s_tmr_poll;
static uint8_t s_led_tmr;

/* ---- real LCP UART bridge used by lcr_host.c lcr_transport() ----
 * port 0 -> USART1 (J1), port 1 -> USART2 (J2). Both are RS232 (PE5 powered). Raw mode so we do
 * synchronous request/response like a real meter link. */
static E_HAL_UART lcr_uart(int port)
{
    /* One meter per port, each on its own connector, same code for both: port 0 = Port 1 (J1,
     * USART1), port 1 = Port 2 (J2, USART2). (An earlier bench shortcut sent both ports over USART2,
     * which made Port 1 look online with nothing plugged into J1.) */
    return (port == 1) ? E_HAL_UART_LCR2 : E_HAL_UART_LCR1;
}

static uint8_t s_rs485;     /* SetRs485: 0 = RS232 transceivers (U504), 1 = RS485 (U104/U4) */

/* Switch the LCR ports between RS232 and RS485. Never power both (they fight on the RX pins); for
 * RS485 the direction pins go to receive (high) before the transceivers are powered. */
/* RS485 is disabled for now (user decision 2026-09-26): the LCR ports always run RS232 and the RS485
 * transceivers stay off. Build with -DLCR_RS485_ENABLE=1 to bring the switching back. */
#ifndef LCR_RS485_ENABLE
#define LCR_RS485_ENABLE 0
#endif

void lcr_set_rs485(uint8_t on)
{
#if !LCR_RS485_ENABLE
    on = 0U;
#endif
    s_rs485 = on ? 1U : 0U;
    HAL_GpioSet(E_HAL_GPIO_RS485_DIR1);
    HAL_GpioSet(E_HAL_GPIO_RS485_DIR2);
    if(s_rs485) {
        HAL_GpioRs232Power(0);
        HAL_GpioRs485Power(1);
    } else {
        HAL_GpioRs485Power(0);
        HAL_GpioRs232Power(1);
    }
}

void lcr_hal_uart_send(int port, const uint8_t *d, uint32_t n)
{
    E_HAL_GPIO_OUT dir = (lcr_uart(port) == E_HAL_UART_LCR2) ? E_HAL_GPIO_RS485_DIR2 : E_HAL_GPIO_RS485_DIR1;
    uint32_t t0;

    if(s_rs485) {
        HAL_GpioReset(dir);                 /* RS485 driver on for the request */
    }
    HAL_UartSend(lcr_uart(port), d, (uint16_t)n);
    HAL_UartFlush(lcr_uart(port));
    if(s_rs485) {
        t0 = millis();                      /* let the last character (0.52 ms at 19200) leave */
        while((millis() - t0) < 2U) {
        }
        HAL_GpioSet(dir);                   /* back to receive for the meter's answer */
    }
}

int lcr_hal_uart_read(int port)
{
    return HAL_UartReadByte(lcr_uart(port));
}

void lcr_hal_uart_flush(int port)
{
    HAL_UartRxFlush(lcr_uart(port));
}

/* reply routing: proto_handle() calls this for every "Lx..." line */
static void lcr_reply(const char *line)
{
    APP_SendReply(line);
}

void APPLCR_ProcessCmd(const uint8_t *data, uint16_t len)
{
    char line[260];

    if(len == 0 || len >= sizeof(line)) {
        return;
    }
    memcpy(line, data, len);
    line[len] = 0;
    proto_handle(line, lcr_reply);
}

static void poll_tick(void *a)
{
    (void)a;
    lcr_host_poll();
    proto_tick();
    /* console oracle (no BLE needed): show each port's meter link + live gross/flow */
    DBG(DBG_I, "LCR p1 node%d %s g=%d.%d f=%d.%d | p2 node%d %s g=%d.%d f=%d.%d",
        lcr_port[0].node, lcr_port[0].online ? "ON" : "off",
        (int)(lcr_port[0].v[0] / 10), (int)(lcr_port[0].v[0] % 10),
        (int)(lcr_port[0].v[1] / 10), (int)(lcr_port[0].v[1] % 10),
        lcr_port[1].node, lcr_port[1].online ? "ON" : "off",
        (int)(lcr_port[1].v[0] / 10), (int)(lcr_port[1].v[0] % 10),
        (int)(lcr_port[1].v[1] / 10), (int)(lcr_port[1].v[1] % 10));
}

/* LCR link LEDs: PC6/PC7 follow the 12 V-present detect inputs (V2.89 applcrLinkCheckTimeOut) */
static void link_led_tick(void *a)
{
    (void)a;
    if(HAL_GpioGetIn(E_HAL_GPIO_IN_PORT1) == 0U) {
        HAL_GpioSet(E_HAL_GPIO_LED_LCR1);
    } else {
        HAL_GpioReset(E_HAL_GPIO_LED_LCR1);
    }
    if(HAL_GpioGetIn(E_HAL_GPIO_IN_PORT2) == 0U) {
        HAL_GpioSet(E_HAL_GPIO_LED_LCR2);
    } else {
        HAL_GpioReset(E_HAL_GPIO_LED_LCR2);
    }
}

void APPLCR_Init(void)
{
    int fails = lcp_selftest();

    DBG(DBG_I, "LCP self-test %s (%d fail)", fails == 0 ? "OK" : "FAIL", fails);

    /* LCR ports: 19200 8N1, RS232 transceivers already powered (PE5=1). Kept for the real-meter
       transport; the simulator does not use the UART. */
    HAL_UartConfig(E_HAL_UART_LCR1, 19200U, 0, 0);
    HAL_UartConfig(E_HAL_UART_LCR2, 19200U, 0, 0);
    /* raw mode: lcr_transport() reads the meter reply byte-by-byte synchronously */
    HAL_UartSetRaw(E_HAL_UART_LCR1, 1);
    HAL_UartSetRaw(E_HAL_UART_LCR2, 1);
    /* RS232 transceivers on (PE5), RS485 off (PE6); receive-direction on the RS485 DE pins */
    lcr_set_rs485(0);

    proto_init();
    lcr_host_init();

    /* if the newest stored record is ahead of the RTC, advance it (Leo keeps time monotonic) */

    TMR_CreatRepeatTimer(1000U, poll_tick, NULL, &s_tmr_poll);
    TMR_Start(s_tmr_poll);
    TMR_CreatRepeatTimer(1000U, link_led_tick, NULL, &s_led_tmr);
    TMR_Start(s_led_tmr);

    DBG(DBG_I, "APPLCR_Init OK!");
}
