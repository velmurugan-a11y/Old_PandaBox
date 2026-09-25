/*
 * led.c - LED state machine, driven by a 100 ms software timer (V2.89 drv/led.c).
 */
#include "drv.h"
#include "sys.h"

/* driver LED -> HAL output pin */
static const E_HAL_GPIO_OUT s_pin[LED_NUM] = {
    E_HAL_GPIO_LED_WIFI, E_HAL_GPIO_LED_GPS, E_HAL_GPIO_LED_BT, E_HAL_GPIO_LED_PWR
};

typedef struct {
    uint8_t mode;
    uint8_t pattern;
    int32_t count;   /* toggles left; -1 = forever */
    uint32_t tick;
} T_LED;

static T_LED s_led[LED_NUM];
static uint8_t s_tmr_id;

/* pattern -> period in 100 ms ticks (0x0801B46C: {0,0,13,9,5,1,0,64}) */
static const uint8_t s_period[8] = {0, 0, 13, 9, 5, 1, 0, 64};

static void led_hw(E_LED l, uint8_t on)
{
    if(on) {
        HAL_GpioSet(s_pin[l]);
    } else {
        HAL_GpioReset(s_pin[l]);
    }
}

static void led_step(void *arg)
{
    uint8_t i;
    (void)arg;

    for(i = 0; i < LED_NUM; i++) {
        T_LED *p = &s_led[i];
        uint8_t period;

        if(p->mode == LED_OFF) {
            continue;
        }
        if(p->mode == LED_ON) {
            led_hw((E_LED)i, 1);
            continue;
        }
        period = (p->pattern < 8) ? s_period[p->pattern] : 0;
        if(period == 0) {
            continue;
        }
        if(++p->tick < period) {
            continue;
        }
        p->tick = 0;
        led_hw((E_LED)i, (uint8_t)(HAL_GpioGetOut(s_pin[i]) ? 0 : 1));
        if(p->count > 0) {
            if(--p->count == 0) {
                led_hw((E_LED)i, 1);   /* patterns end ON, like V2.89 */
                p->mode = LED_HOLD_11;
            }
        }
    }
}

void LED_Init(void)
{
    uint8_t i;

    for(i = 0; i < LED_NUM; i++) {
        s_led[i].mode = LED_OFF;
        led_hw((E_LED)i, 0);
    }
    TMR_CreatRepeatTimer(100U, led_step, NULL, &s_tmr_id);
    TMR_Start(s_tmr_id);
}

void LED_Play(E_LED led, E_LED_MODE mode)
{
    if(led >= LED_NUM) {
        return;
    }
    s_led[led].mode = mode;
    s_led[led].tick = 0;
    switch(mode) {
    case LED_OFF:
        led_hw(led, 0);
        s_led[led].pattern = 0;
        s_led[led].count = 0;
        break;
    case LED_ON:
        led_hw(led, 1);
        s_led[led].pattern = 1;
        s_led[led].count = 0;
        break;
    case LED_BLINK_1300: s_led[led].pattern = 2; s_led[led].count = -1; break;
    case LED_BLINK_900:  s_led[led].pattern = 3; s_led[led].count = -1; break;
    case LED_BLINK_500:  s_led[led].pattern = 4; s_led[led].count = -1; break;
    case LED_BLINK_100:  s_led[led].pattern = 5; s_led[led].count = -1; break;
    case LED_FLASH_1:    s_led[led].pattern = 5; s_led[led].count = -1; break;
    case LED_FLASH_2:    s_led[led].pattern = 5; s_led[led].count = -1; break;
    case LED_FLASH_3:    s_led[led].pattern = 5; s_led[led].count = -1; break;
    case LED_FLASH_4:    s_led[led].pattern = 5; s_led[led].count = -1; break;
    default: break;
    }
}

void LED_Flash(E_LED led, uint8_t n)
{
    if(led >= LED_NUM) {
        return;
    }
    s_led[led].mode = LED_HOLD_11;
    s_led[led].pattern = 5;         /* fast, 100 ms */
    s_led[led].count = 2 * (int32_t)n;
    s_led[led].tick = 0;
    led_hw(led, 0);
}

void LED_Tick(void)
{
    /* the pattern advances on the software timer; nothing to do here */
}

void DRV_Init(void)
{
    uint32_t id;

    LED_Init();
    gd25q256df_init();
    id = gd25q256df_read_id();
    DBG(DBG_I, "u32GdFlashId = %X", (unsigned)id);
}
