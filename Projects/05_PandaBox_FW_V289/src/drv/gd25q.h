/*
 * gd25q.h - GD25Q256 32 MB SPI NOR, 4-byte addressing (same commands as V2.89 drv/gd25q.c).
 */
#ifndef GD25Q_H
#define GD25Q_H

#include <stdint.h>

#define GD25Q_SECTOR_SIZE  4096U
#define GD25Q_PAGE_SIZE    256U
#define GD25Q_TOTAL_SIZE   0x2000000U

uint8_t  gd25q256df_init(void);                     /* reset, enter 4-byte mode */
uint32_t gd25q256df_read_id(void);                  /* JEDEC ID, 0xC84019 */
void     gd25q256df_read_data(uint8_t *buf, uint32_t addr, uint32_t len);
uint8_t  gd25q256df_write_data(const uint8_t *buf, uint32_t addr, uint32_t len);  /* page-split program */
uint8_t  gd25q256df_sector_erase(uint32_t addr);
uint8_t  gd25q256df_chip_erase(void);

/*
 * Write protection for the bench board (standing rule: never erase/write the external flash).
 * While protected (default), erase/program go to a RAM overlay of a few sectors; reads see the
 * overlay first and the real chip otherwise. The firmware behaves exactly as if it had written,
 * but the chip is never touched. Build with -DEXTFLASH_ALLOW_WRITE for real writes.
 */
uint8_t  gd25q_is_protected(void);
uint32_t gd25q_overlay_used(void);

#endif
