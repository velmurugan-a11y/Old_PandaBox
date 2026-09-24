#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include "regs.h"

/* Sets the 4-bit CTL0/CTL1 field for `pin` (0-15) on `port` to `mode`
 * (one of the GPIO_MODE_* constants in regs.h). */
void gpio_set_mode(gpio_reg_t *port, uint8_t pin, uint32_t mode);

/* Also configures as output push-pull and drives it high immediately,
 * so there's no glitch low between "configure" and "first write" on
 * pins like PWRHOLD where that could matter. */
void gpio_set_output_high(gpio_reg_t *port, uint8_t pin);
void gpio_set_output_low(gpio_reg_t *port, uint8_t pin);

void gpio_write(gpio_reg_t *port, uint8_t pin, int level);
int  gpio_read(const gpio_reg_t *port, uint8_t pin);
void gpio_toggle(gpio_reg_t *port, uint8_t pin);

#endif /* GPIO_H */
