#ifndef GD25Q_H
#define GD25Q_H

#include <stdint.h>

#define GD25Q_PAGE_SIZE   256u
#define GD25Q_SECTOR_SIZE 4096u

/* Brings up spi0 and puts the chip into 4-byte address mode (0xB7) --
 * required because 32MB exceeds the 16MB range addressable by the
 * legacy 3-byte-address commands. Every function below then uses
 * 4-byte addresses transparently. */
void gd25q_init(void);

/* Reads the 3-byte JEDEC ID (0x9F): [0]=manufacturer (GigaDevice=0xC8),
 * [1]=memory type, [2]=capacity. This is the empirical check for
 * whether board_config.h's SPI0_FLASH_* pin guess is actually correct
 * -- see gd25q.c's header comment. */
void gd25q_read_jedec_id(uint8_t id[3]);

void gd25q_read(uint32_t addr, uint8_t *buf, uint32_t len);

/* Erases one 4KB sector containing `addr` (address is rounded down to
 * the sector boundary internally). Blocks until the erase completes
 * (typically tens of ms). */
void gd25q_sector_erase(uint32_t addr);

/* Programs up to GD25Q_PAGE_SIZE bytes starting at `addr`. Caller must
 * ensure the target region was erased first and that the write doesn't
 * cross a 256-byte page boundary (the chip wraps within the page rather
 * than continuing into the next one) -- flash_store.c's callers are
 * sized accordingly. Blocks until the program completes. */
void gd25q_page_program(uint32_t addr, const uint8_t *buf, uint32_t len);

#endif /* GD25Q_H */
