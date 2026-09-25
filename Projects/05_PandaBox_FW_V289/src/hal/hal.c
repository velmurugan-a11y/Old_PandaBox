/*
 * hal.c - HAL init, 1 ms tick, delays, watchdog feed, reset reason, jump to application.
 */
#include "hal.h"
#include "sys.h"

static volatile uint32_t s_tick_ms;
static uint32_t s_reset_flags;

void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t HAL_GetTick(void)
{
    return s_tick_ms;
}

void HAL_DelayMs(uint32_t ms)
{
    uint32_t t0 = s_tick_ms;
    while((s_tick_ms - t0) < ms) {
        HAL_FeedWatchDog();
    }
}

void HAL_FeedWatchDog(void)
{
    fwdgt_counter_reload();
}

void HAL_Reboot(void)
{
    NVIC_SystemReset();
}

uint32_t HAL_GetUid0(void)
{
    return *(volatile uint32_t *)0x1FFFF7E8U;
}

/* same order and texts as V2.89 (0x0800B048, strings at 0x0801B580) */
uint32_t HAL_GetResetReason(void)
{
    if(s_reset_flags & RCU_RSTSCK_PORRSTF) {
        return 1;
    }
    if(s_reset_flags & RCU_RSTSCK_SWRSTF) {
        return 2;
    }
    if(s_reset_flags & RCU_RSTSCK_FWDGTRSTF) {
        return 3;
    }
    if(s_reset_flags & RCU_RSTSCK_LPRSTF) {
        return 4;
    }
    if(s_reset_flags & RCU_RSTSCK_EPRSTF) {
        return 0;
    }
    return 5;
}

const char *HAL_GetResetReasonStr(void)
{
    static const char *const names[] = {"Reset PIN", "Power on", "Software reset", "Watchdog reset",
                                        "Low power reset", "Unknown"};
    return names[HAL_GetResetReason()];
}

static void mcu_init(void)
{
    s_reset_flags = RCU_RSTSCK;
    rcu_all_reset_flag_clear();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);
    SysTick_Config(SystemCoreClock / 1000U);
    NVIC_SetPriority(SysTick_IRQn, 0x00U);

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_AF);
}

void HAL_Init(void)
{
    mcu_init();
    GPIO_Initialize();
    UART_Init();
    RTC_Init();
    SPI_Initialize();
    ADC_Initialize();
    FLASH_Init();
    WDG_Init();
}

void HAL_DoEvent(void)
{
    HAL_UartPoll();
}

/* same checks as V2.89 HAL_JumpToApp (0x0800A89C) */
void HAL_JumpToApp(uint32_t addr)
{
    uint32_t sp = *(volatile uint32_t *)addr;
    uint32_t pc = *(volatile uint32_t *)(addr + 4U);

    if((sp & 0x2FFE0000U) != 0x20000000U) {
        return;
    }
    __disable_irq();
    HAL_UartDeinitAll();
    SysTick->CTRL = 0;
    __set_MSP(sp);
    SCB->VTOR = addr;
    ((void (*)(void))pc)();
}
