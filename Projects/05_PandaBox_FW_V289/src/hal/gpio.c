/*
 * gpio.c - pin map and power-on state, identical to V2.89 GPIO_Initialize (0x08015998 / 0x0801594C).
 */
#include "hal.h"

typedef struct {
    uint32_t port;
    uint32_t pin;
} T_PIN;

static const T_PIN s_out[E_HAL_GPIO_OUT_MAX] = {
    {GPIOC, GPIO_PIN_4},  {GPIOB, GPIO_PIN_0},  {GPIOC, GPIO_PIN_5},  {GPIOB, GPIO_PIN_1},
    {GPIOC, GPIO_PIN_6},  {GPIOC, GPIO_PIN_7},  {GPIOE, GPIO_PIN_2},  {GPIOB, GPIO_PIN_15},
    {GPIOD, GPIO_PIN_5},  {GPIOD, GPIO_PIN_4},  {GPIOD, GPIO_PIN_6},  {GPIOA, GPIO_PIN_4},
    {GPIOE, GPIO_PIN_3},  {GPIOE, GPIO_PIN_4},
};

static const T_PIN s_in[E_HAL_GPIO_IN_MAX] = {
    {GPIOB, GPIO_PIN_13}, {GPIOB, GPIO_PIN_12},
};

static void out_pp(uint32_t port, uint32_t pin)
{
    gpio_init(port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, pin);
}

void GPIO_Initialize(void)
{
    /* SW-DP only (JTAG pins free), debug UART on PB6/PB7, LCR2 UART on PD8/PD9 */
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    gpio_pin_remap_config(GPIO_USART0_REMAP, ENABLE);
    gpio_pin_remap_config(GPIO_USART2_FULL_REMAP, ENABLE);

    /* V2.89 order; every output starts low except PE5 (RS232 transceivers on) */
    out_pp(GPIOA, GPIO_PIN_10);                 /* modem DTR */
    out_pp(GPIOD, GPIO_PIN_6);                  /* WiFi power */
    out_pp(GPIOD, GPIO_PIN_5);                  /* BT power */
    out_pp(GPIOD, GPIO_PIN_0);                  /* WDI (external watchdog not fitted) */
    out_pp(GPIOD, GPIO_PIN_4);                  /* BT reset */
    out_pp(GPIOE, GPIO_PIN_2);                  /* modem VGSM */
    out_pp(GPIOB, GPIO_PIN_15);                 /* modem PWRKEY */
    out_pp(GPIOB, GPIO_PIN_1);                  /* LED PWR */
    out_pp(GPIOB, GPIO_PIN_0);                  /* LED GPS */
    out_pp(GPIOC, GPIO_PIN_5);                  /* LED BT */
    out_pp(GPIOC, GPIO_PIN_4);                  /* LED WiFi */
    out_pp(GPIOC, GPIO_PIN_6);                  /* LED LCR1 */
    out_pp(GPIOC, GPIO_PIN_7);                  /* LED LCR2 */
    out_pp(GPIOE, GPIO_PIN_3);                  /* RS485 dir 1 */
    out_pp(GPIOE, GPIO_PIN_4);                  /* RS485 dir 2 */
    out_pp(GPIOE, GPIO_PIN_5);                  /* RS232 power */
    out_pp(GPIOE, GPIO_PIN_6);                  /* RS485 power */
    gpio_bit_set(GPIOE, GPIO_PIN_5);
    gpio_bit_reset(GPIOE, GPIO_PIN_6);

    /* 12 V detect inputs, ADC inputs */
    gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
    gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    gpio_init(GPIOC, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, GPIO_PIN_0 | GPIO_PIN_1);
}

void HAL_GpioSet(E_HAL_GPIO_OUT io)
{
    if(io < E_HAL_GPIO_OUT_MAX) {
        gpio_bit_set(s_out[io].port, s_out[io].pin);
    }
}

void HAL_GpioReset(E_HAL_GPIO_OUT io)
{
    if(io < E_HAL_GPIO_OUT_MAX) {
        gpio_bit_reset(s_out[io].port, s_out[io].pin);
    }
}

uint8_t HAL_GpioGetOut(E_HAL_GPIO_OUT io)
{
    if(io >= E_HAL_GPIO_OUT_MAX) {
        return 0;
    }
    return (GPIO_OCTL(s_out[io].port) & s_out[io].pin) ? 1U : 0U;
}

uint8_t HAL_GpioGetIn(E_HAL_GPIO_IN io)
{
    if(io >= E_HAL_GPIO_IN_MAX) {
        return 0;
    }
    return (GPIO_ISTAT(s_in[io].port) & s_in[io].pin) ? 1U : 0U;
}

void HAL_GpioRs232Power(uint8_t on)
{
    if(on) {
        gpio_bit_set(GPIOE, GPIO_PIN_5);
    } else {
        gpio_bit_reset(GPIOE, GPIO_PIN_5);
    }
}

void HAL_GpioRs485Power(uint8_t on)
{
    if(on) {
        gpio_bit_set(GPIOE, GPIO_PIN_6);
    } else {
        gpio_bit_reset(GPIOE, GPIO_PIN_6);
    }
}
