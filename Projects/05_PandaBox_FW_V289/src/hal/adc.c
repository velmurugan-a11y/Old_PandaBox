/*
 * adc.c - ADC0 single conversions: ch10 = PC0 (12 V in, /11), ch11 = PC1 (coin cell, /2 while PE1 = 1).
 */
#include "hal.h"

static const uint8_t s_chan[E_HAL_ADC_MAX] = {ADC_CHANNEL_10, ADC_CHANNEL_11};

void ADC_Initialize(void)
{
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV8);
    adc_deinit(ADC0);
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 1U);
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC0_1_2_EXTTRIG_ROUTINE_NONE);
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, ENABLE);
    adc_enable(ADC0);
    HAL_DelayMs(1);
    adc_calibration_enable(ADC0);
}

uint16_t HAL_AdcGetValue(E_HAL_ADC ch)
{
    uint32_t t0;

    if(ch >= E_HAL_ADC_MAX) {
        return 0;
    }
    adc_routine_channel_config(ADC0, 0U, s_chan[ch], ADC_SAMPLETIME_239POINT5);
    adc_flag_clear(ADC0, ADC_FLAG_EOC);
    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
    t0 = HAL_GetTick();
    while(!adc_flag_get(ADC0, ADC_FLAG_EOC)) {
        if((HAL_GetTick() - t0) > 5U) {
            return 0;
        }
    }
    return (uint16_t)adc_routine_data_read(ADC0);
}
