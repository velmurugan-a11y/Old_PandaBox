/*
 * rtc.c - RTC on the 32.768 kHz crystal, counter = Unix time in seconds.
 * Same backup marker as V2.89 (BKP data 0 = 0xA5A5 means "already configured", 0x0800B3D0),
 * so the time kept by the coin cell survives a swap between Leo's firmware and this one.
 */
#include "hal.h"

#define RTC_BKP_MARK      0xA5A5U
#define RTC_DEFAULT_TIME  0x657F0909U   /* 2023-12-17, V2.89 default */

void RTC_Init(void)
{
    rcu_periph_clock_enable(RCU_BKPI);
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    if(bkp_read_data(BKP_DATA_0) != RTC_BKP_MARK) {
        bkp_deinit();
        rcu_osci_on(RCU_LXTAL);
        rcu_osci_stab_wait(RCU_LXTAL);
        rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
        rcu_periph_clock_enable(RCU_RTC);
        rtc_register_sync_wait();
        rtc_lwoff_wait();
        rtc_prescaler_set(32767U);
        rtc_lwoff_wait();
        rtc_counter_set(RTC_DEFAULT_TIME);
        rtc_lwoff_wait();
        bkp_write_data(BKP_DATA_0, RTC_BKP_MARK);
    } else {
        rtc_register_sync_wait();
        rtc_lwoff_wait();
    }
    /* V2.89 0x08014554 / 0x08014568: BKP_OCTL bit7 set, tamper pin disabled */
    BKP_OCTL |= 0x0080U;
    BKP_TPCTL &= (uint16_t)~0x0001U;
}

uint32_t RTC_GetSec(void)
{
    return rtc_counter_get();
}

void RTC_SetSec(uint32_t sec)
{
    rcu_periph_clock_enable(RCU_BKPI);
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();
    rtc_lwoff_wait();
    rtc_counter_set(sec);
    rtc_lwoff_wait();
}
