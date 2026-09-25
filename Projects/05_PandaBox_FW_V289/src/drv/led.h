/*
 * led.h - LED patterns (V2.89 drv/led.c).
 */
#ifndef LED_H
#define LED_H

#include <stdint.h>

typedef enum {
    E_LED_WIFI = 0,   /* PC4 */
    E_LED_BT,         /* PC5 */
    E_LED_GPS,        /* PB0 */
    E_LED_PWR,        /* PB1 */
    E_LED_MAX
} E_LED;

typedef enum {
    E_LED_OFF = 0,
    E_LED_ON = 1,
    E_LED_SLOW = 2,     /* toggle every 1.3 s */
    E_LED_MID = 3,      /* 0.9 s */
    E_LED_FAST = 4,     /* 0.5 s */
    E_LED_QUICK = 5,    /* 0.1 s */
    E_LED_FLASH1 = 6,   /* 1..4 quick flashes, 1.3 s pause, repeat */
    E_LED_FLASH2 = 7,
    E_LED_FLASH3 = 8,
    E_LED_FLASH4 = 9,
    E_LED_HOLD = 10,
    E_LED_BLINKN = 11
} E_LED_MODE;

void LED_Init(void);
void LED_Play(uint8_t led, uint8_t mode);
void LED_Flash(uint8_t led, uint8_t times);   /* activity blink, ends ON */

#endif
