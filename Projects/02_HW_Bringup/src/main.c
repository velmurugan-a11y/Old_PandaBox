/*!
    \file    main.c
    \brief   PandaBox hardware bring-up firmware: serial CLI on USART0 (PB6/PB7, 115200 8N1)

    Every test prints one machine-readable summary line:
        RESULT <test> PASS|FAIL|INFO <details>
    so tools/bringup.py can drive the board and log the results.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "uart.h"
#include "tests.h"

#define FW_NAME     "PandaBox HW bring-up"
#define FW_VERSION  "0.1"
#define LINE_MAX_LEN 128U

static volatile uint32_t ms_tick = 0U;

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
    uint32_t start = ms_tick;
    while((ms_tick - start) < ms) {
    }
}

/* newlib-nano printf -> USART0 */
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

static uint32_t reset_flags;

uint32_t boot_reset_flags(void)
{
    return reset_flags;
}

static void banner(void)
{
    SystemCoreClockUpdate();
    printf("\n\n==== %s v%s (%s %s) ====\n", FW_NAME, FW_VERSION, __DATE__, __TIME__);
    printf("SystemCoreClock=%lu Hz, HXTAL %s\n", (unsigned long)SystemCoreClock,
           (RCU_CTL & RCU_CTL_HXTALSTB) ? "OK (12 MHz)" : "FAILED -> running on IRC8M");
    printf("type 'help' for commands\n");
}

int main(void)
{
    char line[LINE_MAX_LEN];
    uint32_t n = 0U;
    int c;

    reset_flags = RCU_RSTSCK;
    rcu_all_reset_flag_clear();

    board_init();
    SysTick_Config(SystemCoreClock / 1000U);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);
    uart_init(PORT_CON, 115200U);
    setvbuf(stdout, NULL, _IONBF, 0);   /* prompt "> " has no newline: don't let newlib hold it back */

    gpio_bit_set(LED_PWR_PORT, LED_PWR_PIN);
    banner();
    printf("> ");

    while(1) {
        c = uart_getc(PORT_CON);
        if(c < 0) {
            continue;
        }
        if(c == '\r' || c == '\n') {
            if(n == 0U) {
                continue;
            }
            line[n] = '\0';
            printf("\n");
            tests_dispatch(line);
            n = 0U;
            printf("> ");
        } else if((c == 0x08 || c == 0x7F) && n > 0U) {
            n--;
            printf("\b \b");
        } else if(c >= 0x20 && n < (LINE_MAX_LEN - 1U)) {
            line[n++] = (char)c;
            uart_write(PORT_CON, (const uint8_t *)&c, 1U);
        }
    }
}
