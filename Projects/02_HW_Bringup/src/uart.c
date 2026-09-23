/*!
    \file    uart.c
    \brief   interrupt-driven RX ring buffers for the five UARTs, blocking TX
*/

#include "uart.h"
#include "board.h"

#define RX_BUF_SIZE     1024U   /* power of two */

typedef struct {
    uint32_t periph;
    IRQn_Type irq;
    volatile uint8_t buf[RX_BUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overruns;
} uart_ctx_t;

static uart_ctx_t ctx[PORT_COUNT] = {
    {USART0, USART0_IRQn},
    {USART1, USART1_IRQn},
    {USART2, USART2_IRQn},
    {UART3,  UART3_IRQn},
    {UART4,  UART4_IRQn},
};

static void uart_pins_init(uart_port_t port)
{
    switch(port) {
    case PORT_CON:
        rcu_periph_clock_enable(RCU_USART0);
        gpio_init(CON_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, CON_TX_PIN);
        gpio_init(CON_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, CON_RX_PIN);
        break;
    case PORT_LCR1:
        rcu_periph_clock_enable(RCU_USART1);
        gpio_init(LCR1_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LCR1_TX_PIN);
        gpio_init(LCR1_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, LCR1_RX_PIN);
        break;
    case PORT_LCR2:
        rcu_periph_clock_enable(RCU_USART2);
        gpio_init(LCR2_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LCR2_TX_PIN);
        gpio_init(LCR2_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, LCR2_RX_PIN);
        break;
    case PORT_BT:
        rcu_periph_clock_enable(RCU_UART3);
        gpio_init(BT_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, BT_TX_PIN);
        gpio_init(BT_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, BT_RX_PIN);
        break;
    case PORT_GSM:
        rcu_periph_clock_enable(RCU_UART4);
        gpio_init(GSM_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GSM_TX_PIN);
        gpio_init(GSM_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GSM_RX_PIN);
        break;
    default:
        break;
    }
}

void uart_init(uart_port_t port, uint32_t baud)
{
    uart_ctx_t *c = &ctx[port];

    uart_pins_init(port);
    usart_deinit(c->periph);
    usart_baudrate_set(c->periph, baud);
    usart_word_length_set(c->periph, USART_WL_8BIT);
    usart_stop_bit_set(c->periph, USART_STB_1BIT);
    usart_parity_config(c->periph, USART_PM_NONE);
    usart_hardware_flow_rts_config(c->periph, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(c->periph, USART_CTS_DISABLE);
    usart_receive_config(c->periph, USART_RECEIVE_ENABLE);
    usart_transmit_config(c->periph, USART_TRANSMIT_ENABLE);
    c->head = c->tail = 0U;
    c->overruns = 0U;
    usart_interrupt_enable(c->periph, USART_INT_RBNE);
    nvic_irq_enable(c->irq, 1U, 0U);
    usart_enable(c->periph);
}

void uart_deinit(uart_port_t port)
{
    uart_ctx_t *c = &ctx[port];

    nvic_irq_disable(c->irq);
    usart_disable(c->periph);
}

void uart_write(uart_port_t port, const uint8_t *data, uint32_t len)
{
    uint32_t periph = ctx[port].periph;

    while(len--) {
        while(RESET == usart_flag_get(periph, USART_FLAG_TBE)) {
        }
        usart_data_transmit(periph, *data++);
    }
}

void uart_puts(uart_port_t port, const char *s)
{
    while(*s) {
        uart_write(port, (const uint8_t *)s, 1U);
        s++;
    }
}

void uart_wait_tx_done(uart_port_t port)
{
    while(RESET == usart_flag_get(ctx[port].periph, USART_FLAG_TC)) {
    }
}

int uart_getc(uart_port_t port)
{
    uart_ctx_t *c = &ctx[port];
    uint8_t b;

    if(c->head == c->tail) {
        return -1;
    }
    b = c->buf[c->tail];
    c->tail = (c->tail + 1U) & (RX_BUF_SIZE - 1U);
    return b;
}

uint32_t uart_available(uart_port_t port)
{
    uart_ctx_t *c = &ctx[port];
    return (c->head - c->tail) & (RX_BUF_SIZE - 1U);
}

void uart_flush_rx(uart_port_t port)
{
    ctx[port].tail = ctx[port].head;
}

uint32_t uart_overruns(uart_port_t port)
{
    return ctx[port].overruns;
}

static void uart_isr(uart_port_t port)
{
    uart_ctx_t *c = &ctx[port];
    uint32_t next;
    uint8_t b;

    if(RESET != usart_interrupt_flag_get(c->periph, USART_INT_FLAG_RBNE) ||
       RESET != usart_flag_get(c->periph, USART_FLAG_ORERR)) {
        /* reading DATA clears RBNE and (after a STAT read) ORERR */
        b = (uint8_t)usart_data_receive(c->periph);
        next = (c->head + 1U) & (RX_BUF_SIZE - 1U);
        if(next != c->tail) {
            c->buf[c->head] = b;
            c->head = next;
        } else {
            c->overruns++;
        }
    }
}

void USART0_IRQHandler(void) { uart_isr(PORT_CON); }
void USART1_IRQHandler(void) { uart_isr(PORT_LCR1); }
void USART2_IRQHandler(void) { uart_isr(PORT_LCR2); }
void UART3_IRQHandler(void)  { uart_isr(PORT_BT); }
void UART4_IRQHandler(void)  { uart_isr(PORT_GSM); }
