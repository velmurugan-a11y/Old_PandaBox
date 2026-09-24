/*
 * spi0.c -- bare register-level SPI0 driver for the GD25Q256E flash chip.
 *
 * Pin assignment (board_config.h's SPI0_FLASH_*) is UNCONFIRMED -- no
 * schematic/BOM exists in this repo for this chip. gd25q.c's boot-time
 * JEDEC ID readback is what actually verifies these pins on real
 * hardware; this file itself makes no claim about correctness beyond
 * "this is how you drive whatever is wired to these 4 pins."
 */
#include "spi0.h"
#include "regs.h"
#include "board_config.h"
#include "gpio.h"

void spi0_init(void)
{
    /* CS idle high (flash deselected) before anything else touches it. */
    gpio_set_output_high(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_CS_PIN);

    /* SCK/MOSI: AF push-pull output. MISO: floating input. */
    gpio_set_mode(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_SCK_PIN, GPIO_MODE_AF_PP_50MHZ);
    gpio_set_mode(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_MOSI_PIN, GPIO_MODE_AF_PP_50MHZ);
    gpio_set_mode(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_MISO_PIN, GPIO_MODE_IN_FLOAT);

    /* Master, CPOL=0/CPHA=0 (mode 0 -- GD25Q256E supports mode 0 and 3),
     * MSB-first, PCLK2/32 (120MHz/32 = 3.75MHz -- conservative bring-up
     * speed, well under the chip's 104MHz max; raise once verified).
     * Software NSS management (SWNSSEN+SWNSS=1) since CS is driven as a
     * plain GPIO, not the hardware NSS pin -- required or the peripheral
     * refuses to stay in master mode. */
    SPI0_BASE->CTL0 = SPI_CTL0_MSTMOD | SPI_CTL0_PSC_DIV32
                     | SPI_CTL0_SWNSSEN | SPI_CTL0_SWNSS;
    SPI0_BASE->CTL0 |= SPI_CTL0_SPIEN;
}

void spi0_cs_low(void)
{
    gpio_write(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_CS_PIN, 0);
}

void spi0_cs_high(void)
{
    gpio_write(SPI0_FLASH_GPIO_PORT, SPI0_FLASH_CS_PIN, 1);
}

uint8_t spi0_transfer_byte(uint8_t tx)
{
    uint32_t timeout;

    timeout = 100000u;
    while (!(SPI0_BASE->STAT & SPI_STAT_TBE) && --timeout) { }

    SPI0_BASE->DATA = tx;

    timeout = 100000u;
    while (!(SPI0_BASE->STAT & SPI_STAT_RBNE) && --timeout) { }

    return (uint8_t)SPI0_BASE->DATA;
}
