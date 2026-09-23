/*!
    \file    board.c
    \brief   safe power-on state for every control pin (docs/PandaBox_Peripheral_Bringup.md section 1)
*/

#include "board.h"

static void out_pp(uint32_t port, uint32_t pin, FlagStatus level)
{
    gpio_bit_write(port, pin, level);
    gpio_init(port, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, pin);
}

void board_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_AF);

    /* SW-DP only (frees PA15/PB3/PB4), USART0 on PB6/PB7, USART2 on PD8/PD9 */
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    gpio_pin_remap_config(GPIO_USART0_REMAP, ENABLE);
    gpio_pin_remap_config(GPIO_USART2_FULL_REMAP, ENABLE);

    /* modem and WiFi off */
    out_pp(GSM_PWR_PORT, GSM_PWR_PIN, RESET);
    out_pp(GSM_PWRKEY_PORT, GSM_PWRKEY_PIN, RESET);
    out_pp(GSM_DTR_PORT, GSM_DTR_PIN, RESET);
    out_pp(WIFI_EN_PORT, WIFI_EN_PIN, RESET);

    /* Bluetooth: power on (its default), held in reset until started */
    out_pp(BT_EN_PORT, BT_EN_PIN, SET);
    out_pp(BT_RST_PORT, BT_RST_PIN, RESET);

    /* LCR transceivers: RS485 direction = receive first, then both rails off */
    out_pp(RS485_DIR1_PORT, RS485_DIR1_PIN, SET);
    out_pp(RS485_DIR2_PORT, RS485_DIR2_PIN, SET);
    out_pp(RS232_EN_PORT, RS232_EN_PIN, RESET);
    out_pp(RS485_EN_PORT, RS485_EN_PIN, RESET);

    /* coin cell disconnected from divider, flash deselected */
    out_pp(BATT_EN_PORT, BATT_EN_PIN, RESET);
    out_pp(FLASH_CS_PORT, FLASH_CS_PIN, SET);

    /* LEDs off */
    out_pp(LED_PWR_PORT, LED_PWR_PIN, RESET);
    out_pp(LED_GPS_PORT, LED_GPS_PIN, RESET);
    out_pp(LED_WIFI_PORT, LED_WIFI_PIN, RESET);
    out_pp(LED_BT_PORT, LED_BT_PIN, RESET);
    out_pp(LED_LCP1_PORT, LED_LCP1_PIN, RESET);
    out_pp(LED_LCP2_PORT, LED_LCP2_PIN, RESET);

    /* inputs (external pull-ups / dividers fitted) */
    gpio_init(DET_LCP1_12V_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, DET_LCP1_12V_PIN);
    gpio_init(DET_LCP2_12V_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, DET_LCP2_12V_PIN);
    gpio_init(DET_DB9_12V_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, DET_DB9_12V_PIN);
    gpio_init(DET_VBUS_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, DET_VBUS_PIN);
}
