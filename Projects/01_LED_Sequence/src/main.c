/*!
    \file    main.c
    \brief   PandaBox 6-LED serial (chase) toggle on GD32F305VCT6

    LEDs are driven through S9014 NPN transistors (1K base resistor, 10K pull-down),
    so every LED is ACTIVE HIGH: pin = 1 -> LED ON, pin = 0 -> LED OFF.

        LED            MCU pin
        PWR  red       PB1
        GPS  green     PB0
        WiFi orange    PC4
        BT   blue      PC5
        LCP1           PC6
        LCP2           PC7

    Sequence: each LED is switched on alone for LED_ON_TIME_MS, in the order above,
    then the pattern repeats forever.
*/

#include "gd32f30x.h"

#define LED_ON_TIME_MS      300U

typedef struct {
    uint32_t port;
    uint32_t pin;
    rcu_periph_enum clk;
} led_t;

static const led_t leds[] = {
    {GPIOB, GPIO_PIN_1, RCU_GPIOB},     /* PWR  red    */
    {GPIOB, GPIO_PIN_0, RCU_GPIOB},     /* GPS  green  */
    {GPIOC, GPIO_PIN_4, RCU_GPIOC},     /* WiFi orange */
    {GPIOC, GPIO_PIN_5, RCU_GPIOC},     /* BT   blue   */
    {GPIOC, GPIO_PIN_6, RCU_GPIOC},     /* LCP1        */
    {GPIOC, GPIO_PIN_7, RCU_GPIOC},     /* LCP2        */
};

#define LED_COUNT   (sizeof(leds) / sizeof(leds[0]))

/* volatile globals so the sequence can be observed with a debugger / J-Link */
static volatile uint32_t ms_tick = 0U;
volatile uint32_t led_index = 0U;
volatile uint32_t led_steps = 0U;

void SysTick_Handler(void)
{
    ms_tick++;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = ms_tick;
    while((ms_tick - start) < ms) {
    }
}

static void led_init(void)
{
    uint32_t i;

    for(i = 0U; i < LED_COUNT; i++) {
        rcu_periph_clock_enable(leds[i].clk);
        gpio_bit_reset(leds[i].port, leds[i].pin);
        gpio_init(leds[i].port, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, leds[i].pin);
    }
}

static void led_all_off(void)
{
    uint32_t i;

    for(i = 0U; i < LED_COUNT; i++) {
        gpio_bit_reset(leds[i].port, leds[i].pin);
    }
}

int main(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
    led_init();

    while(1) {
        led_all_off();
        gpio_bit_set(leds[led_index].port, leds[led_index].pin);
        delay_ms(LED_ON_TIME_MS);

        led_index = (led_index + 1U) % LED_COUNT;
        led_steps++;
    }
}
