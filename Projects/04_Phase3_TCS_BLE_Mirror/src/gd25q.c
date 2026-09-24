/*
 * gd25q.c -- GD25Q256E (32MB SPI NOR flash) chip driver, on top of spi0.c.
 *
 * Command opcodes (0x9F/0x06/0x05/0x02/0x20/0x03/0xB7) are standard
 * JEDEC/GigaDevice SPI-NOR opcodes, not board-specific -- these are safe
 * to trust regardless of the pin-assignment uncertainty. What's NOT yet
 * confirmed is that these bytes reach the right chip at all; see
 * gd25q_read_jedec_id()'s use in flash_store.c's boot-time self-test.
 */
#include "gd25q.h"
#include "spi0.h"

#define GD25Q_CMD_WRITE_ENABLE   0x06u
#define GD25Q_CMD_READ_STATUS1   0x05u
#define GD25Q_CMD_PAGE_PROGRAM   0x02u
#define GD25Q_CMD_SECTOR_ERASE   0x20u
#define GD25Q_CMD_READ_DATA      0x03u
#define GD25Q_CMD_READ_JEDEC_ID  0x9Fu
#define GD25Q_CMD_ENTER_4BYTE    0xB7u

#define GD25Q_STATUS1_WIP        (1u << 0) /* write-in-progress */

static void send_addr4(uint32_t addr)
{
    spi0_transfer_byte((uint8_t)(addr >> 24));
    spi0_transfer_byte((uint8_t)(addr >> 16));
    spi0_transfer_byte((uint8_t)(addr >> 8));
    spi0_transfer_byte((uint8_t)addr);
}

static void write_enable(void)
{
    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_WRITE_ENABLE);
    spi0_cs_high();
}

static void wait_ready(void)
{
    uint8_t status;
    uint32_t timeout = 1000000u; /* generous -- sector erase can take ~50-100ms */

    do {
        spi0_cs_low();
        spi0_transfer_byte(GD25Q_CMD_READ_STATUS1);
        status = spi0_transfer_byte(0xFFu);
        spi0_cs_high();
    } while ((status & GD25Q_STATUS1_WIP) && --timeout);
}

void gd25q_init(void)
{
    spi0_init();

    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_ENTER_4BYTE);
    spi0_cs_high();
}

void gd25q_read_jedec_id(uint8_t id[3])
{
    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_READ_JEDEC_ID);
    id[0] = spi0_transfer_byte(0xFFu);
    id[1] = spi0_transfer_byte(0xFFu);
    id[2] = spi0_transfer_byte(0xFFu);
    spi0_cs_high();
}

void gd25q_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_READ_DATA);
    send_addr4(addr);
    for (i = 0; i < len; i++) {
        buf[i] = spi0_transfer_byte(0xFFu);
    }
    spi0_cs_high();
}

void gd25q_sector_erase(uint32_t addr)
{
    uint32_t sector_addr = addr & ~(GD25Q_SECTOR_SIZE - 1u);

    write_enable();
    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_SECTOR_ERASE);
    send_addr4(sector_addr);
    spi0_cs_high();
    wait_ready();
}

void gd25q_page_program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    write_enable();
    spi0_cs_low();
    spi0_transfer_byte(GD25Q_CMD_PAGE_PROGRAM);
    send_addr4(addr);
    for (i = 0; i < len; i++) {
        spi0_transfer_byte(buf[i]);
    }
    spi0_cs_high();
    wait_ready();
}
