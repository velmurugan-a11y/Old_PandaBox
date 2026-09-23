/*!
    \file    uart.h
    \brief   interrupt-driven RX ring buffers for the five UARTs
*/

#ifndef UART_H
#define UART_H

#include <stdint.h>

typedef enum {
    PORT_CON = 0,   /* USART0 debug console */
    PORT_LCR1,      /* USART1 */
    PORT_LCR2,      /* USART2 */
    PORT_BT,        /* UART3  */
    PORT_GSM,       /* UART4  */
    PORT_COUNT
} uart_port_t;

void     uart_init(uart_port_t port, uint32_t baud);
void     uart_deinit(uart_port_t port);
void     uart_write(uart_port_t port, const uint8_t *data, uint32_t len);
void     uart_puts(uart_port_t port, const char *s);
int      uart_getc(uart_port_t port);           /* -1 if empty */
uint32_t uart_available(uart_port_t port);
void     uart_flush_rx(uart_port_t port);
void     uart_wait_tx_done(uart_port_t port);   /* waits for TC (needed before RS485 turnaround) */
uint32_t uart_overruns(uart_port_t port);

#endif /* UART_H */
