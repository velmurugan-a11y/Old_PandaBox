#ifndef SPI0_H
#define SPI0_H

#include <stdint.h>

/* Bare register-level SPI0 driver, master mode, CPOL=0/CPHA=0 (mode 0),
 * MSB-first, software-controlled CS (SPI0_FLASH_CS_PIN driven directly
 * via GPIO, not the hardware NSS signal). Pin assignment is UNCONFIRMED
 * -- see board_config.h's SPI0_FLASH_* comment. */
void spi0_init(void);

void spi0_cs_low(void);
void spi0_cs_high(void);

/* Full-duplex single-byte transfer: shifts `tx` out while shifting the
 * received byte back in. Polled, no interrupt (this workload is small
 * bursts, not a continuous stream). */
uint8_t spi0_transfer_byte(uint8_t tx);

#endif /* SPI0_H */
