/*
 * spi.c - SPI0 master on PA5/PA6/PA7 for the GD25Q256 (CS = PA4 via the GPIO enum), mode 3 like V2.89.
 */
#include "hal.h"

void SPI_Initialize(void)
{
    spi_parameter_struct p;

    rcu_periph_clock_enable(RCU_SPI0);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_4);
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_7);
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
    gpio_bit_set(GPIOA, GPIO_PIN_4);

    spi_i2s_deinit(SPI0);
    spi_struct_para_init(&p);
    p.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    p.device_mode = SPI_MASTER;
    p.frame_size = SPI_FRAMESIZE_8BIT;
    p.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    p.nss = SPI_NSS_SOFT;
    p.prescale = SPI_PSC_4;
    p.endian = SPI_ENDIAN_MSB;
    spi_init(SPI0, &p);
    spi_enable(SPI0);
}

uint8_t HAL_SpiTransfer(uint8_t b)
{
    while(RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE)) {
    }
    spi_i2s_data_transmit(SPI0, b);
    while(RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE)) {
    }
    return (uint8_t)spi_i2s_data_receive(SPI0);
}
