/*!
    \file    board.h
    \brief   PandaBox XBOX V2.5 pin map (GD32F305VCT6, LQFP100)

    Source: docs/PandaBox_Peripheral_Bringup.md (PCB-derived, cross-checked with BOM).
    Numbers in comments are LQFP100 pin numbers.
*/

#ifndef BOARD_H
#define BOARD_H

#include "gd32f30x.h"

/* ---- LEDs (S9014 NPN drivers, active high) ---- */
#define LED_PWR_PORT        GPIOB   /* PB1  36 red    */
#define LED_PWR_PIN         GPIO_PIN_1
#define LED_GPS_PORT        GPIOB   /* PB0  35 green  */
#define LED_GPS_PIN         GPIO_PIN_0
#define LED_WIFI_PORT       GPIOC   /* PC4  33 orange */
#define LED_WIFI_PIN        GPIO_PIN_4
#define LED_BT_PORT         GPIOC   /* PC5  34 blue   */
#define LED_BT_PIN          GPIO_PIN_5
#define LED_LCP1_PORT       GPIOC   /* PC6  63        */
#define LED_LCP1_PIN        GPIO_PIN_6
#define LED_LCP2_PORT       GPIOC   /* PC7  64        */
#define LED_LCP2_PIN        GPIO_PIN_7

/* ---- Debug console: USART0 remapped, PB6 92 TX / PB7 93 RX ---- */
#define CON_UART            USART0
#define CON_TX_PORT         GPIOB
#define CON_TX_PIN          GPIO_PIN_6
#define CON_RX_PORT         GPIOB
#define CON_RX_PIN          GPIO_PIN_7

/* ---- LCR port 1: USART1 PA2 25 TX / PA3 26 RX ---- */
#define LCR1_UART           USART1
#define LCR1_TX_PORT        GPIOA
#define LCR1_TX_PIN         GPIO_PIN_2
#define LCR1_RX_PORT        GPIOA
#define LCR1_RX_PIN         GPIO_PIN_3

/* ---- LCR port 2: USART2 full remap PD8 55 TX / PD9 56 RX ---- */
#define LCR2_UART           USART2
#define LCR2_TX_PORT        GPIOD
#define LCR2_TX_PIN         GPIO_PIN_8
#define LCR2_RX_PORT        GPIOD
#define LCR2_RX_PIN         GPIO_PIN_9

/* ---- LCR transceiver control ---- */
#define RS232_EN_PORT       GPIOE   /* PE5 4: RS232_3.3V LDO (U504), high = on */
#define RS232_EN_PIN        GPIO_PIN_5
#define RS485_EN_PORT       GPIOE   /* PE6 5: RS485_3.3V LDO (U104, U4), high = on */
#define RS485_EN_PIN        GPIO_PIN_6
#define RS485_DIR1_PORT     GPIOE   /* PE3 2: port 1 direction, high = receive */
#define RS485_DIR1_PIN      GPIO_PIN_3
#define RS485_DIR2_PORT     GPIOE   /* PE4 3: port 2 direction, high = receive */
#define RS485_DIR2_PIN      GPIO_PIN_4

/* ---- Bluetooth YC1021: UART3 PC10 78 TX / PC11 79 RX ---- */
#define BT_UART             UART3
#define BT_TX_PORT          GPIOC
#define BT_TX_PIN           GPIO_PIN_10
#define BT_RX_PORT          GPIOC
#define BT_RX_PIN           GPIO_PIN_11
#define BT_RST_PORT         GPIOD   /* PD4 85: YC1021 RESET, low = reset */
#define BT_RST_PIN          GPIO_PIN_4
#define BT_EN_PORT          GPIOD   /* PD5 86: BT_3.3V LDO, high = on (pulled up by default) */
#define BT_EN_PIN           GPIO_PIN_5

/* ---- 4G modem EC25: UART4 PC12 80 TX / PD2 83 RX (via U603, powered by modem VDD_EXT) ---- */
#define GSM_UART            UART4
#define GSM_TX_PORT         GPIOC
#define GSM_TX_PIN          GPIO_PIN_12
#define GSM_RX_PORT         GPIOD
#define GSM_RX_PIN          GPIO_PIN_2
#define GSM_PWR_PORT        GPIOE   /* PE2 1: VGSM LDO enable, high = on */
#define GSM_PWR_PIN         GPIO_PIN_2
#define GSM_PWRKEY_PORT     GPIOB   /* PB15 54: PWRKEY via NPN, high = key pressed */
#define GSM_PWRKEY_PIN      GPIO_PIN_15
#define GSM_DTR_PORT        GPIOA   /* PA10 69: DTR (divider), keep low */
#define GSM_DTR_PIN         GPIO_PIN_10

/* ---- WiFi FC20N power (hosted by EC25) ---- */
#define WIFI_EN_PORT        GPIOD   /* PD6 87: WLAN_3V3 LDO, high = on */
#define WIFI_EN_PIN         GPIO_PIN_6

/* ---- External SPI NOR GD25Q256E: SPI0 PA5 30 SCK / PA6 31 MISO / PA7 32 MOSI, CS PA4 29 ---- */
#define FLASH_SPI           SPI0
#define FLASH_CS_PORT       GPIOA
#define FLASH_CS_PIN        GPIO_PIN_4
#define FLASH_SPI_PORT      GPIOA
#define FLASH_SCK_PIN       GPIO_PIN_5
#define FLASH_MISO_PIN      GPIO_PIN_6
#define FLASH_MOSI_PIN      GPIO_PIN_7

/* ---- Analog ---- */
#define ADC_12V_PORT        GPIOC   /* PC0 15: ADC01_IN10, V12 = Vadc * 11 */
#define ADC_12V_PIN         GPIO_PIN_0
#define ADC_12V_CH          ADC_CHANNEL_10
#define ADC_CELL_PORT       GPIOC   /* PC1 16: ADC01_IN11, Vcell = Vadc * 2 (only while BATT_EN = 1) */
#define ADC_CELL_PIN        GPIO_PIN_1
#define ADC_CELL_CH         ADC_CHANNEL_11
#define BATT_EN_PORT        GPIOE   /* PE1 98: connect coin cell to divider, high = on */
#define BATT_EN_PIN         GPIO_PIN_1

/* ---- Digital inputs ---- */
#define DET_LCP1_12V_PORT   GPIOB   /* PB13 52: 12 V on LCR port 1 pin 13, low = present */
#define DET_LCP1_12V_PIN    GPIO_PIN_13
#define DET_LCP2_12V_PORT   GPIOB   /* PB12 51: 12 V on LCR port 2 pin 13, low = present */
#define DET_LCP2_12V_PIN    GPIO_PIN_12
#define DET_DB9_12V_PORT    GPIOE   /* PE11 42: 12 V on DB9 pin 8, low = present */
#define DET_DB9_12V_PIN     GPIO_PIN_11
#define DET_VBUS_PORT       GPIOA   /* PA9 68: USB-C VBUS, high = present */
#define DET_VBUS_PIN        GPIO_PIN_9

void board_init(void);

#endif /* BOARD_H */
