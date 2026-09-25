/*
 * hal.h - MCU hardware layer, same enums and behaviour as Leo's src/hal (X-Box V2.89).
 */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include "gd32f30x.h"

/* ---- GPIO outputs: index order = V2.89 table at 0x08015D02 ---- */
typedef enum {
    E_HAL_GPIO_LED_WIFI = 0,   /* PC4 */
    E_HAL_GPIO_LED_GPS,        /* PB0 */
    E_HAL_GPIO_LED_BT,         /* PC5 */
    E_HAL_GPIO_LED_PWR,        /* PB1 */
    E_HAL_GPIO_LED_LCR1,       /* PC6 */
    E_HAL_GPIO_LED_LCR2,       /* PC7 */
    E_HAL_GPIO_GSM_PWR,        /* PE2  VGSM LDO enable */
    E_HAL_GPIO_GSM_PWRKEY,     /* PB15 */
    E_HAL_GPIO_BT_EN,          /* PD5 */
    E_HAL_GPIO_BT_RST,         /* PD4  high = run */
    E_HAL_GPIO_WIFI_EN,        /* PD6 */
    E_HAL_GPIO_FLASH_CS,       /* PA4 */
    E_HAL_GPIO_RS485_DIR1,     /* PE3  high = receive */
    E_HAL_GPIO_RS485_DIR2,     /* PE4 */
    E_HAL_GPIO_OUT_MAX
} E_HAL_GPIO_OUT;

typedef enum {
    E_HAL_GPIO_IN_PORT1 = 0,   /* PB13  low = 12 V present on LCR port 1 */
    E_HAL_GPIO_IN_PORT2,       /* PB12 */
    E_HAL_GPIO_IN_MAX
} E_HAL_GPIO_IN;

typedef enum {
    E_HAL_ADC_VCC12 = 0,       /* PC0 ch10, V = adc * 11 */
    E_HAL_ADC_BAT,             /* PC1 ch11 */
    E_HAL_ADC_MAX
} E_HAL_ADC;

/* ---- UARTs: enum = peripheral index ---- */
typedef enum {
    E_HAL_UART_PRINT = 0,      /* USART0 PB6/PB7, debug + CLI 115200 */
    E_HAL_UART_LCR1,           /* USART1 PA2/PA3 19200 */
    E_HAL_UART_LCR2,           /* USART2 PD8/PD9 19200 */
    E_HAL_UART_BT,             /* UART3  PC10/PC11 115200 */
    E_HAL_UART_EC20,           /* UART4  PC12/PD2 115200 */
    E_HAL_UART_MAX
} E_HAL_UART;

typedef void (*HAL_UART_RX_CB)(uint8_t *data, uint16_t len);
typedef void (*HAL_UART_TXDONE_CB)(void);

/* hal.c */
void     HAL_Init(void);
void     HAL_DoEvent(void);
void     HAL_DelayMs(uint32_t ms);
uint32_t HAL_GetTick(void);
void     HAL_FeedWatchDog(void);
void     HAL_Reboot(void);
void     HAL_JumpToApp(uint32_t addr);
uint32_t HAL_GetResetReason(void);
const char *HAL_GetResetReasonStr(void);
uint32_t HAL_GetUid0(void);

/* gpio.c */
void    GPIO_Initialize(void);
void    HAL_GpioSet(E_HAL_GPIO_OUT io);
void    HAL_GpioReset(E_HAL_GPIO_OUT io);
uint8_t HAL_GpioGetOut(E_HAL_GPIO_OUT io);
uint8_t HAL_GpioGetIn(E_HAL_GPIO_IN io);
void    HAL_GpioRs232Power(uint8_t on);   /* PE5 */
void    HAL_GpioRs485Power(uint8_t on);   /* PE6 */

/* uart.c */
void    UART_Init(void);
int     HAL_UartConfig(E_HAL_UART u, uint32_t baud, uint8_t parity, uint8_t stop);
void    HAL_UartSetPackInterval(E_HAL_UART u, uint16_t ms);
void    HAL_UartSetCallback(E_HAL_UART u, HAL_UART_RX_CB rx, HAL_UART_TXDONE_CB txdone);
int     HAL_UartSend(E_HAL_UART u, const void *data, uint16_t len);
/* raw byte access for synchronous init sequences (e.g. YC1021 patch upload); when raw is on the
 * packet callback is suppressed and bytes are popped one at a time with HAL_UartReadByte. */
void    HAL_UartSetRaw(E_HAL_UART u, uint8_t on);
int     HAL_UartReadByte(E_HAL_UART u);   /* -1 when empty */
void    HAL_UartRxFlush(E_HAL_UART u);
void    HAL_UartFlush(E_HAL_UART u);
void    HAL_UartPoll(void);
uint8_t HAL_UartIsBusy(E_HAL_UART u);
void    HAL_UartDeinitAll(void);

/* flash.c (internal) */
#define FLASH_PAGE_SIZE          2048U
#define FLASH_APP_INFO_ADDR      0x08007800U
#define FLASH_APP1_ADDR          0x08008000U
#define FLASH_APP2_ADDR          0x08021000U
#define FLASH_APP_SIZE           0x19000U
#define FLASH_USER_DATA_ADDR     0x0803F800U
void FLASH_Init(void);
int  HAL_FlashUserDataRead(uint32_t offset, void *buf, uint32_t len);
int  HAL_FlashUserDataWrite(uint32_t offset, const void *buf, uint32_t len);
int  HAL_FlashRead(uint32_t addr, void *buf, uint32_t len);
int  HAL_FlashErasePage(uint32_t addr);
int  HAL_FlashProgram(uint32_t addr, const void *buf, uint32_t len);

/* rtc.c */
void     RTC_Init(void);
uint32_t RTC_GetSec(void);
void     RTC_SetSec(uint32_t sec);

/* adc.c */
void     ADC_Initialize(void);
uint16_t HAL_AdcGetValue(E_HAL_ADC ch);

/* spi.c */
void    SPI_Initialize(void);
uint8_t HAL_SpiTransfer(uint8_t b);

/* wdg.c */
void WDG_Init(void);

#endif
