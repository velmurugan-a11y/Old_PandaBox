#include "gpio.h"

#define GPIO_MODE_OUT_PP_50MHZ 0x3u /* 0b0011: MODE=11 (50MHz), CTL=00 (push-pull) */

void gpio_set_mode(gpio_reg_t *port, uint8_t pin, uint32_t mode)
{
    volatile uint32_t *ctl = (pin < 8) ? &port->CTL0 : &port->CTL1;
    uint8_t shift = (uint8_t)((pin % 8) * 4);
    uint32_t v = *ctl;
    v &= ~(0xFu << shift);
    v |= (mode & 0xFu) << shift;
    *ctl = v;
}

void gpio_set_output_high(gpio_reg_t *port, uint8_t pin)
{
    port->BOP = (1u << pin); /* set before switching mode: no glitch low */
    gpio_set_mode(port, pin, GPIO_MODE_OUT_PP_50MHZ);
}

void gpio_set_output_low(gpio_reg_t *port, uint8_t pin)
{
    port->BC = (1u << pin);
    gpio_set_mode(port, pin, GPIO_MODE_OUT_PP_50MHZ);
}

void gpio_write(gpio_reg_t *port, uint8_t pin, int level)
{
    if (level) {
        port->BOP = (1u << pin);
    } else {
        port->BC = (1u << pin);
    }
}

int gpio_read(const gpio_reg_t *port, uint8_t pin)
{
    return (port->ISTAT >> pin) & 1u;
}

void gpio_toggle(gpio_reg_t *port, uint8_t pin)
{
    if (port->OCTL & (1u << pin)) {
        port->BC = (1u << pin);
    } else {
        port->BOP = (1u << pin);
    }
}
