/*
 * flash.c - internal flash: user-config page (last 2 KB), APP_INFO page, application slots.
 * Programming is by half-word like Leo's ProgramDataToFlash.
 */
#include <string.h>
#include "hal.h"

void FLASH_Init(void)
{
}

int HAL_FlashRead(uint32_t addr, void *buf, uint32_t len)
{
    memcpy(buf, (const void *)addr, len);
    return 0;
}

int HAL_FlashErasePage(uint32_t addr)
{
    fmc_state_enum st;

    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_BANK0_END | FMC_FLAG_BANK0_WPERR | FMC_FLAG_BANK0_PGERR);
    st = fmc_page_erase(addr);
    fmc_lock();
    return (st == FMC_READY) ? 0 : -1;
}

int HAL_FlashProgram(uint32_t addr, const void *buf, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)buf;
    uint32_t i;
    int rc = 0;

    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_BANK0_END | FMC_FLAG_BANK0_WPERR | FMC_FLAG_BANK0_PGERR);
    for(i = 0; i < len; i += 2U) {
        uint16_t hw = b[i];
        hw |= (uint16_t)(((i + 1U) < len ? b[i + 1U] : 0xFFU) << 8);
        if(fmc_halfword_program(addr + i, hw) != FMC_READY) {
            rc = -1;
            break;
        }
    }
    fmc_lock();
    return rc;
}

int HAL_FlashUserDataRead(uint32_t offset, void *buf, uint32_t len)
{
    return HAL_FlashRead(FLASH_USER_DATA_ADDR + offset, buf, len);
}

int HAL_FlashUserDataWrite(uint32_t offset, const void *buf, uint32_t len)
{
    if(HAL_FlashErasePage(FLASH_USER_DATA_ADDR) != 0) {
        return -1;
    }
    return HAL_FlashProgram(FLASH_USER_DATA_ADDR + offset, buf, len);
}
