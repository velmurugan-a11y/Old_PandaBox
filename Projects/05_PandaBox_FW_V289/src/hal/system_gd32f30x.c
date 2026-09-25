/*
 * system_gd32f30x.c - clock and vector-table setup, identical to X-Box V2.89 (0x0800B714 / 0x08016BE4).
 *
 * 12 MHz HXTAL -> PREDV1 /3 -> PLL1 x10 (40 MHz) -> PREDV0 /10 (4 MHz) -> PLL x30 = 120 MHz.
 * AHB /1 = 120 MHz, APB2 /1 = 120 MHz, APB1 /2 = 60 MHz, LDO high-drive for 120 MHz.
 */
#include "gd32f30x.h"

uint32_t SystemCoreClock = 120000000U;

extern uint32_t __gVectors[];

static void system_clock_120m_hxtal12(void)
{
    uint32_t timeout = 0U;

    RCU_CTL |= RCU_CTL_HXTALEN;
    while((0U == (RCU_CTL & RCU_CTL_HXTALSTB)) && (timeout++ < 0xFFFFFU)) {
    }
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)) {
        /* no crystal: stay on IRC8M, like Leo's code would hang we just keep running slow */
        return;
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;

    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* PLL source = PREDV0, PLL x30 (bit29 = PLLMF[4], PLLMF[3:0] = 13) */
    RCU_CFG0 &= 0x9FC3FFFFU;
    RCU_CFG0 |= 0x20350000U;

    /* PREDV0 /10, PREDV1 /3, PLL1 x10, PREDV0 source = PLL1, PLL pre-source = HXTAL */
    RCU_CFG1 &= 0xBFFEF000U;
    RCU_CFG1 |= 0x00010829U;

    RCU_CTL |= RCU_CTL_PLL1EN;
    while(0U == (RCU_CTL & RCU_CTL_PLL1STB)) {
    }
    RCU_CTL |= RCU_CTL_PLLEN;
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)) {
    }

    PMU_CTL |= PMU_CTL_HDEN;
    while(0U == (PMU_CS & PMU_CS_HDRF)) {
    }
    PMU_CTL |= PMU_CTL_HDS;
    while(0U == (PMU_CS & PMU_CS_HDSRF)) {
    }

    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)) {
    }
}

void SystemInit(void)
{
    /* FPU full access (the binary sets CPACR even though the code is soft-float) */
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));

    /* reset the RCU to its default state */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)) {
    }
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_CKMEN | RCU_CTL_PLLEN | RCU_CTL_HXTALBPS);
    RCU_CFG0 &= ~(RCU_CFG0_SCS | RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC |
                  RCU_CFG0_ADCPSC | RCU_CFG0_PLLSEL | RCU_CFG0_PLLMF | RCU_CFG0_USBFSPSC |
                  RCU_CFG0_CKOUT0SEL | RCU_CFG0_ADCPSC_2 | RCU_CFG0_PLLMF_4);
    RCU_CTL &= ~(RCU_CTL_PLL1EN | RCU_CTL_PLL2EN);
    RCU_CFG1 = 0x00000000U;
    RCU_INT = 0x00FF0000U;

    system_clock_120m_hxtal12();

    /* vector table of this image (APP1, APP2 or bench) */
    SCB->VTOR = (uint32_t)__gVectors;
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = 120000000U;
}
