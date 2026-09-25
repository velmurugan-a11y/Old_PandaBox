/*
 * uart.c - five UARTs, interrupt driven.
 *
 * RX: bytes collect in a buffer; a packet is handed to the callback from the main loop once the
 *     line has been idle for the "pack interval" (Leo: EC20 20 ms, BT 20 ms, LCR 50 ms, debug 50 ms).
 * TX: ring buffer, sent from the TBE interrupt; the TX-done callback runs (in the main loop) after
 *     the last stop bit (TC), which is what the RS485 direction switch needs.
 */
#include <string.h>
#include "hal.h"

#define UART_RX_SIZE  1024U
#define UART_TX_SIZE  2048U

typedef struct {
    uint32_t periph;
    IRQn_Type irq;
    volatile uint16_t rx_len;
    volatile uint32_t rx_tick;
    uint16_t pack_ms;
    uint8_t rx_buf[UART_RX_SIZE];
    volatile uint16_t tx_head, tx_tail;
    volatile uint8_t tx_active;
    volatile uint8_t tx_done;
    uint8_t tx_buf[UART_TX_SIZE];
    HAL_UART_RX_CB rx_cb;
    HAL_UART_TXDONE_CB tx_cb;
    uint8_t enabled;
    uint8_t raw;
    volatile uint16_t raw_rd;   /* read index into rx_buf while raw */
} T_UART;

static T_UART s_uart[E_HAL_UART_MAX];
static uint8_t s_pkt[UART_RX_SIZE + 1];

static const uint32_t s_periph[E_HAL_UART_MAX] = {USART0, USART1, USART2, UART3, UART4};
static const IRQn_Type s_irq[E_HAL_UART_MAX] = {USART0_IRQn, USART1_IRQn, USART2_IRQn, UART3_IRQn, UART4_IRQn};

static void uart_pins(E_HAL_UART u)
{
    switch(u) {
    case E_HAL_UART_PRINT:
        rcu_periph_clock_enable(RCU_USART0);
        gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
        gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_7);
        break;
    case E_HAL_UART_LCR1:
        rcu_periph_clock_enable(RCU_USART1);
        gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
        gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_3);
        break;
    case E_HAL_UART_LCR2:
        rcu_periph_clock_enable(RCU_USART2);
        gpio_init(GPIOD, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_8);
        gpio_init(GPIOD, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
        break;
    case E_HAL_UART_BT:
        rcu_periph_clock_enable(RCU_UART3);
        gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_10);
        gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_11);
        break;
    case E_HAL_UART_EC20:
        rcu_periph_clock_enable(RCU_UART4);
        gpio_init(GPIOC, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
        gpio_init(GPIOD, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
        break;
    default:
        break;
    }
}

void UART_Init(void)
{
    uint32_t i;

    memset(s_uart, 0, sizeof(s_uart));
    for(i = 0; i < E_HAL_UART_MAX; i++) {
        s_uart[i].periph = s_periph[i];
        s_uart[i].irq = s_irq[i];
        s_uart[i].pack_ms = 20U;
    }
}

int HAL_UartConfig(E_HAL_UART u, uint32_t baud, uint8_t parity, uint8_t stop)
{
    T_UART *p;

    if(u >= E_HAL_UART_MAX) {
        return -1;
    }
    p = &s_uart[u];
    uart_pins(u);
    usart_deinit(p->periph);
    usart_baudrate_set(p->periph, baud);
    usart_word_length_set(p->periph, parity ? USART_WL_9BIT : USART_WL_8BIT);
    usart_parity_config(p->periph, parity == 1U ? USART_PM_ODD : (parity == 2U ? USART_PM_EVEN : USART_PM_NONE));
    usart_stop_bit_set(p->periph, stop ? USART_STB_2BIT : USART_STB_1BIT);
    usart_receive_config(p->periph, USART_RECEIVE_ENABLE);
    usart_transmit_config(p->periph, USART_TRANSMIT_ENABLE);
    usart_interrupt_enable(p->periph, USART_INT_RBNE);
    usart_enable(p->periph);
    nvic_irq_enable(p->irq, 1, (uint8_t)u);
    p->rx_len = 0;
    p->tx_head = p->tx_tail = 0;
    p->tx_active = 0;
    p->enabled = 1;
    return 0;
}

void HAL_UartSetPackInterval(E_HAL_UART u, uint16_t ms)
{
    if(u < E_HAL_UART_MAX) {
        s_uart[u].pack_ms = ms;
    }
}

void HAL_UartSetCallback(E_HAL_UART u, HAL_UART_RX_CB rx, HAL_UART_TXDONE_CB txdone)
{
    if(u < E_HAL_UART_MAX) {
        s_uart[u].rx_cb = rx;
        s_uart[u].tx_cb = txdone;
    }
}

static uint16_t tx_free(const T_UART *p)
{
    uint16_t used = (uint16_t)((p->tx_head - p->tx_tail + UART_TX_SIZE) % UART_TX_SIZE);
    return (uint16_t)(UART_TX_SIZE - 1U - used);
}

int HAL_UartSend(E_HAL_UART u, const void *data, uint16_t len)
{
    const uint8_t *b = (const uint8_t *)data;
    T_UART *p;
    uint16_t i;

    if(u >= E_HAL_UART_MAX || !s_uart[u].enabled || data == NULL) {
        return -1;
    }
    p = &s_uart[u];
    for(i = 0; i < len; i++) {
        while(tx_free(p) == 0U) {
            HAL_FeedWatchDog();
        }
        p->tx_buf[p->tx_head] = b[i];
        __disable_irq();
        p->tx_head = (uint16_t)((p->tx_head + 1U) % UART_TX_SIZE);
        if(!p->tx_active) {
            p->tx_active = 1;
            p->tx_done = 0;
            usart_interrupt_disable(p->periph, USART_INT_TC);
        }
        usart_interrupt_enable(p->periph, USART_INT_TBE);
        __enable_irq();
    }
    return 0;
}

void HAL_UartFlush(E_HAL_UART u)
{
    if(u < E_HAL_UART_MAX && s_uart[u].enabled) {
        while(s_uart[u].tx_active) {
            HAL_FeedWatchDog();
        }
    }
}

uint8_t HAL_UartIsBusy(E_HAL_UART u)
{
    return (u < E_HAL_UART_MAX) ? s_uart[u].tx_active : 0U;
}

void HAL_UartDeinitAll(void)
{
    uint32_t i;

    for(i = 0; i < E_HAL_UART_MAX; i++) {
        if(s_uart[i].enabled) {
            HAL_UartFlush((E_HAL_UART)i);
            nvic_irq_disable(s_uart[i].irq);
            usart_deinit(s_uart[i].periph);
            s_uart[i].enabled = 0;
        }
    }
}

void HAL_UartSetRaw(E_HAL_UART u, uint8_t on)
{
    if(u < E_HAL_UART_MAX) {
        __disable_irq();
        s_uart[u].raw = on;
        s_uart[u].raw_rd = 0;
        s_uart[u].rx_len = 0;
        __enable_irq();
    }
}

int HAL_UartReadByte(E_HAL_UART u)
{
    T_UART *p;
    int c = -1;

    if(u >= E_HAL_UART_MAX) {
        return -1;
    }
    p = &s_uart[u];
    __disable_irq();
    if(p->raw_rd < p->rx_len) {
        c = p->rx_buf[p->raw_rd++];
        if(p->raw_rd >= p->rx_len) {
            p->raw_rd = 0;
            p->rx_len = 0;
        }
    }
    __enable_irq();
    return c;
}

void HAL_UartRxFlush(E_HAL_UART u)
{
    if(u < E_HAL_UART_MAX) {
        __disable_irq();
        s_uart[u].rx_len = 0;
        s_uart[u].raw_rd = 0;
        __enable_irq();
    }
}

void HAL_UartPoll(void)
{
    uint32_t i;
    uint16_t n;

    for(i = 0; i < E_HAL_UART_MAX; i++) {
        T_UART *p = &s_uart[i];
        if(!p->enabled) {
            continue;
        }
        if(p->tx_done) {
            p->tx_done = 0;
            if(p->tx_cb) {
                p->tx_cb();
            }
        }
        if(p->raw) {
            continue;
        }
        if(p->rx_len && (HAL_GetTick() - p->rx_tick) >= p->pack_ms) {
            __disable_irq();
            n = p->rx_len;
            memcpy(s_pkt, p->rx_buf, n);
            p->rx_len = 0;
            __enable_irq();
            s_pkt[n] = 0;
            if(p->rx_cb) {
                p->rx_cb(s_pkt, n);
            }
        }
    }
}

static void uart_isr(E_HAL_UART u)
{
    T_UART *p = &s_uart[u];
    uint32_t per = p->periph;

    if(RESET != usart_interrupt_flag_get(per, USART_INT_FLAG_RBNE) ||
       RESET != usart_flag_get(per, USART_FLAG_ORERR)) {
        uint8_t c = (uint8_t)usart_data_receive(per);
        if(p->rx_len < UART_RX_SIZE) {
            p->rx_buf[p->rx_len++] = c;
        }
        p->rx_tick = HAL_GetTick();
    }
    if((USART_CTL0(per) & USART_CTL0_TBEIE) && RESET != usart_flag_get(per, USART_FLAG_TBE)) {
        if(p->tx_tail != p->tx_head) {
            usart_data_transmit(per, p->tx_buf[p->tx_tail]);
            p->tx_tail = (uint16_t)((p->tx_tail + 1U) % UART_TX_SIZE);
        } else {
            usart_interrupt_disable(per, USART_INT_TBE);
            usart_interrupt_enable(per, USART_INT_TC);
        }
    }
    if((USART_CTL0(per) & USART_CTL0_TCIE) && RESET != usart_flag_get(per, USART_FLAG_TC)) {
        usart_interrupt_disable(per, USART_INT_TC);
        if(p->tx_tail == p->tx_head) {
            p->tx_active = 0;
            p->tx_done = 1;
        } else {
            usart_interrupt_enable(per, USART_INT_TBE);
        }
    }
}

void USART0_IRQHandler(void) { uart_isr(E_HAL_UART_PRINT); }
void USART1_IRQHandler(void) { uart_isr(E_HAL_UART_LCR1); }
void USART2_IRQHandler(void) { uart_isr(E_HAL_UART_LCR2); }
void UART3_IRQHandler(void)  { uart_isr(E_HAL_UART_BT); }
void UART4_IRQHandler(void)  { uart_isr(E_HAL_UART_EC20); }
