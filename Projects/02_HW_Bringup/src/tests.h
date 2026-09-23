/*!
    \file    tests.h
    \brief   bring-up test commands
*/

#ifndef TESTS_H
#define TESTS_H

#include <stdint.h>

void     tests_dispatch(char *line);
uint32_t millis(void);
void     delay_ms(uint32_t ms);
uint32_t boot_reset_flags(void);

#endif /* TESTS_H */
