#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "regs.h"

typedef struct {
    usart_reg_t *base;
    gpio_reg_t  *tx_port;
    uint8_t      tx_pin;
    gpio_reg_t  *rx_port;
    uint8_t      rx_pin;
} uart_port_t;

/* Configures TX (AF push-pull) / RX (floating input) pins and the
 * peripheral's baud rate, then enables TX+RX. Caller must have already
 * enabled the relevant GPIO/USART clocks (see clock_init) and applied any
 * needed AFIO remap BEFORE calling this, since remap must be set before
 * or while the pins are reconfigured, not after. */
void uart_init(const uart_port_t *p, uint32_t baud, uint32_t pclk_hz);

void uart_putc(const uart_port_t *p, char c);
void uart_puts(const uart_port_t *p, const char *s);
void uart_write(const uart_port_t *p, const uint8_t *data, uint32_t len);

/* Returns 1 if a received byte is waiting, 0 otherwise -- non-blocking,
 * safe to poll in a loop across multiple UARTs. */
int uart_data_ready(const uart_port_t *p);

/* Only valid immediately after uart_data_ready() returned 1. */
uint8_t uart_getc(const uart_port_t *p);

#endif /* UART_H */
