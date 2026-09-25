/*
 * drv.h - off-chip drivers: LED patterns and the GD25Q256 external flash.
 */
#ifndef DRV_H
#define DRV_H

#include <stdint.h>
#include "hal.h"

/* ---- LEDs (index = LED driver enum, maps onto HAL LED pins) ---- */
typedef enum {
    LED_WIFI = 0,   /* PC4 */
    LED_GPS,        /* PB0 */
    LED_BT,         /* PC5 */
    LED_PWR,        /* PB1 */
    LED_NUM         /* the 4 driver LEDs; LCR LEDs are driven directly by app_lcr */
} E_LED;

/* LED_Play modes, same as V2.89 (0x0801600C): */
typedef enum {
    LED_OFF = 0,
    LED_ON = 1,
    LED_BLINK_1300 = 2,   /* toggle every 1.3 s */
    LED_BLINK_900  = 3,
    LED_BLINK_500  = 4,
    LED_BLINK_100  = 5,   /* fast */
    LED_FLASH_1    = 6,   /* 1 fast flash then a 1.3 s pause, repeating */
    LED_FLASH_2    = 7,
    LED_FLASH_3    = 8,
    LED_FLASH_4    = 9,
    LED_HOLD_10    = 10,
    LED_HOLD_11    = 11
} E_LED_MODE;

void DRV_Init(void);
void LED_Init(void);
void LED_Play(E_LED led, E_LED_MODE mode);
void LED_Flash(E_LED led, uint8_t n);   /* n activity blinks, ends ON */
void LED_Tick(void);                     /* called from the 100 ms timer */

/* ---- GD25Q256 (32 MB) external NOR flash on SPI0, 4-byte addressing ---- */
#define GD25Q_PAGE    256U
#define GD25Q_SECTOR  4096U
#define GD25Q_SIZE    0x2000000U   /* 32 MB */

int      gd25q256df_init(void);
uint32_t gd25q256df_read_id(void);
void     gd25q256df_read(uint32_t addr, void *buf, uint32_t len);
int      gd25q256df_write_page(uint32_t addr, const void *buf, uint32_t len);
int      gd25q256df_sector_erase(uint32_t addr);
int      gd25q256df_chip_erase(void);

#endif
