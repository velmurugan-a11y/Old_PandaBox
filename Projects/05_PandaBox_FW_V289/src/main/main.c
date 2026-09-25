/*
 * main.c - PandaBox V2.89-compatible firmware entry and super-loop (V2.89 main at 0x08016254).
 *
 *   HAL_Init -> SYS_Init -> DRV_Init -> APP_Init (3 s timer -> module inits) -> CLI_Init
 *   while(1){ HAL_FeedWatchDog(); HAL_DoEvent(); EVT_DoEvent(); TMR_ProcessTimeout(); }
 */
#include "hal.h"
#include "sys.h"
#include "drv.h"
#include "app.h"
#include <string.h>
#include "app_compat.h"

void CLI_Init(void);

/* ---- app_compat time base (shared with proto/lcr_host from Projects/03) ---- */
static uint32_t s_epoch_base, s_epoch_base_ms;

uint32_t millis(void)
{
    return HAL_GetTick();
}

void delay_ms(uint32_t ms)
{
    HAL_DelayMs(ms);
}

uint32_t app_time(void)
{
    uint32_t rtc = RTC_GetSec();
    if(rtc > 1577836800U) {         /* RTC valid (after 2020) -> use it */
        return rtc;
    }
    return s_epoch_base + (HAL_GetTick() - s_epoch_base_ms) / 1000U;
}

void app_set_time(uint32_t epoch)
{
    RTC_SetSec(epoch);
    s_epoch_base = epoch;
    s_epoch_base_ms = HAL_GetTick();
}

const char *app_build_date(void)
{
    return FW_DATE_STR;
}

void bt_set_name(const char *name)
{
    APPBT_SetBtName(name, (uint16_t)strlen(name));
}

int main(void)
{
    s_epoch_base = 1735689600U;      /* 2025-01-01 fallback if the RTC is unset */
    s_epoch_base_ms = 0;

    HAL_Init();
    SYS_Init();
    DRV_Init();
    APP_Init();
    CLI_Init();

    for(;;) {
        HAL_FeedWatchDog();
        HAL_DoEvent();
        EVT_DoEvent();
        TMR_ProcessTimeout();
    }
}
