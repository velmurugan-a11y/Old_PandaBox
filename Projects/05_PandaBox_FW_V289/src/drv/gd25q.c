/*
 * gd25q.c - GD25Q256 (32 MB) SPI NOR flash, byte sequences copied from V2.89 drv/gd25q.c.
 * 4-byte addressing throughout: 0x0B read, 0x02 program, 0x20 sector erase, 0x9F id, 0x66/0x99 reset.
 */
#include <string.h>
#include "drv.h"
#include "sys.h"

#define CS_LOW()   HAL_GpioReset(E_HAL_GPIO_FLASH_CS)
#define CS_HIGH()  HAL_GpioSet(E_HAL_GPIO_FLASH_CS)

static uint8_t xfer(uint8_t b)
{
    return HAL_SpiTransfer(b);
}

static void send_addr(uint32_t addr)
{
    xfer((uint8_t)(addr >> 24));
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
}

static uint8_t read_status(uint8_t reg)   /* reg = 05 (SR1), 35 (SR2), 15 (SR3) */
{
    uint8_t v;
    CS_LOW();
    xfer(reg);
    v = xfer(0xFF);
    CS_HIGH();
    return v;
}

static void write_enable(void)
{
    CS_LOW();
    xfer(0x06);
    CS_HIGH();
}

/* poll SR1.WIP; returns 1 on success, 0 on timeout (like V2.89 fn_0801577C) */
static int wait_wip(void)
{
    uint32_t i = 0;
    while(++i <= 0x00FFFF00U) {
        if((read_status(0x05) & 0x01U) == 0U) {
            return 1;
        }
    }
    DBG(DBG_E, "wait write end fail!");
    return 0;
}

int gd25q256df_init(void)
{
    /* reset enable + reset (0x66 / 0x99), then wait idle */
    CS_LOW(); xfer(0x66); CS_HIGH();
    CS_LOW(); xfer(0x99); CS_HIGH();
    HAL_DelayMs(1);
    return wait_wip();
}

uint32_t gd25q256df_read_id(void)
{
    uint32_t id;
    CS_LOW();
    xfer(0x9F);
    id = (uint32_t)xfer(0xFF) << 16;
    id |= (uint32_t)xfer(0xFF) << 8;
    id |= xfer(0xFF);
    CS_HIGH();
    return id;
}

void gd25q256df_read(uint32_t addr, void *buf, uint32_t len)
{
    uint8_t *b = (uint8_t *)buf;
    uint32_t i;

    CS_LOW();
    xfer(0x0B);            /* fast read */
    send_addr(addr);
    xfer(0xFF);            /* dummy */
    for(i = 0; i < len; i++) {
        b[i] = xfer(0xA5);
    }
    CS_HIGH();
}

static int page_program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    write_enable();
    CS_LOW();
    xfer(0x02);
    send_addr(addr);
    for(i = 0; i < len; i++) {
        xfer(buf[i]);
    }
    CS_HIGH();
    return wait_wip();
}

int gd25q256df_write_page(uint32_t addr, const void *buf, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)buf;
    uint32_t off = addr & (GD25Q_PAGE - 1U);
    uint32_t chunk;

    while(len > 0U) {
        chunk = GD25Q_PAGE - off;
        if(chunk > len) {
            chunk = len;
        }
        if(!page_program(addr, b, chunk)) {
            return 0;
        }
        addr += chunk;
        b += chunk;
        len -= chunk;
        off = 0;
    }
    return 1;
}

int gd25q256df_sector_erase(uint32_t addr)
{
    write_enable();
    if(!wait_wip()) {
        return 0;
    }
    CS_LOW();
    xfer(0x20);
    send_addr(addr);
    CS_HIGH();
    return wait_wip();
}

int gd25q256df_chip_erase(void)
{
    write_enable();
    CS_LOW();
    xfer(0xC7);
    CS_HIGH();
    return wait_wip();
}
