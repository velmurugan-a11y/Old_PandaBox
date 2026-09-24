#include "uart.h"
#include "gpio.h"

static void set_baud(usart_reg_t *u, uint32_t baud, uint32_t pclk_hz)
{
    /* Standard STM32F1/GD32F1-3 USART baud generator: BAUD register is a
     * 12-bit mantissa + 4-bit fraction (1/16 resolution) of
     * USARTDIV = pclk / (16 * baud). Integer-only, rounded to nearest
     * 1/16th to avoid pulling in float support on this build. */
    uint32_t scaled = (25u * pclk_hz) / (4u * baud); /* = 100 * USARTDIV */
    uint32_t mantissa = scaled / 100u;
    uint32_t fraction = ((scaled - mantissa * 100u) * 16u + 50u) / 100u;
    if (fraction > 15u) { /* carry from rounding */
        fraction = 0u;
        mantissa += 1u;
    }
    u->BAUD = (mantissa << 4) | (fraction & 0xFu);
}

void uart_init(const uart_port_t *p, uint32_t baud, uint32_t pclk_hz)
{
    gpio_set_mode(p->tx_port, p->tx_pin, GPIO_MODE_AF_PP_50MHZ);
    gpio_set_mode(p->rx_port, p->rx_pin, GPIO_MODE_IN_FLOAT);

    set_baud(p->base, baud, pclk_hz);
    p->base->CTL0 = USART_CTL0_UEN | USART_CTL0_TEN | USART_CTL0_REN;
}

void uart_putc(const uart_port_t *p, char c)
{
    while ((p->base->STAT & USART_STAT_TBE) == 0) {
        /* wait for transmit buffer empty */
    }
    p->base->DATA = (uint8_t)c;
}

void uart_puts(const uart_port_t *p, const char *s)
{
    while (*s) {
        uart_putc(p, *s++);
    }
}

void uart_write(const uart_port_t *p, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uart_putc(p, (char)data[i]);
    }
}

int uart_data_ready(const uart_port_t *p)
{
    return (p->base->STAT & USART_STAT_RBNE) ? 1 : 0;
}

uint8_t uart_getc(const uart_port_t *p)
{
    return (uint8_t)p->base->DATA;
}
