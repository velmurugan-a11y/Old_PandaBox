#include "clock.h"
#include "regs.h"
#include "board_config.h"

/* Phase 2: switch SYSCLK from the uncalibrated internal 8MHz RC
 * oscillator (IRC8M) to HXTAL(12MHz, X101) -> PLL -> 120MHz, per the
 * PandaBox master hardware note's explicit rule 1: "HXTAL_VALUE =
 * 12000000 and the PLL set for 12 MHz -> 120 MHz. The GD library's
 * default CL settings assume 25MHz and would overclock the chip to
 * 250MHz." This had been deliberately deferred through this project's
 * "Phase 1" (see clock.h) the whole session -- running on IRC8M's
 * imprecise, uncalibrated frequency the whole time is a real, concrete
 * suspect for the "consistent-but-wrong single byte" pattern seen from
 * EC25 once it was actually powered (see ec25.c's VGSM/PE2 fix):
 * that's a classic UART baud-mismatch symptom against an external chip
 * with its own accurate clock, while RTT (no UART peripheral involved
 * at all) stayed clean throughout, which is consistent with the error
 * being specific to UART bit timing, not general logic correctness.
 *
 * Returns 1 if the switch succeeded, 0 if HXTAL never stabilized
 * (bounded retry, not an infinite loop -- a real crystal fault should
 * leave the board running on IRC8M rather than hang forever). */
static int clock_switch_to_pll_120mhz(void)
{
    volatile uint32_t timeout;

    /* Flash wait states MUST be raised before SYSCLK actually moves to
     * 120MHz -- flash can't keep up with reads at that speed otherwise,
     * and doing this after the switch risks fetching garbage/hanging. */
    FMC->WS = (FMC->WS & ~FMC_WS_WSCNT_Msk) | FMC_WS_WSCNT_3;

    RCU->CTL |= RCU_CTL_HXTALEN;
    timeout = 0x000FFFFFu;
    while (!(RCU->CTL & RCU_CTL_HXTALSTB)) {
        if (--timeout == 0u) {
            return 0; /* X101 never stabilized -- stay on IRC8M rather than hang */
        }
    }

    /* AHB=/1 (120MHz), APB1=/2 (60MHz, its max on this family), APB2=/1
     * (120MHz) -- the standard GigaDevice-documented split for running
     * GD32F30x parts at their full 120MHz. */
    RCU->CFG0 = (RCU->CFG0 & ~(RCU_CFG0_AHBPSC_Msk | RCU_CFG0_APB1PSC_Msk | RCU_CFG0_APB2PSC_Msk))
              | RCU_CFG0_AHBPSC_DIV1 | RCU_CFG0_APB1PSC_DIV2 | RCU_CFG0_APB2PSC_DIV1;

    /* PLL input = HXTAL undivided (PLLPREDV=0), PLLMF=x10 -> 12MHz*10 = 120MHz. */
    RCU->CFG0 = (RCU->CFG0 & ~(RCU_CFG0_PLLSEL | RCU_CFG0_PLLPREDV | RCU_CFG0_PLLMF_Msk))
              | RCU_CFG0_PLLSEL | RCU_CFG0_PLLMF_MUL10;

    RCU->CTL |= RCU_CTL_PLLEN;
    timeout = 0x000FFFFFu;
    while (!(RCU->CTL & RCU_CTL_PLLSTB)) {
        if (--timeout == 0u) {
            return 0; /* PLL never locked -- stay on IRC8M */
        }
    }

    RCU->CFG0 = (RCU->CFG0 & ~RCU_CFG0_SCS_Msk) | RCU_CFG0_SCS_PLL;
    timeout = 0x000FFFFFu;
    while ((RCU->CFG0 & RCU_CFG0_SCSS_Msk) != RCU_CFG0_SCSS_PLL) {
        if (--timeout == 0u) {
            return 0; /* switch never took effect */
        }
    }

    return 1;
}

static int s_clock_is_pll;

int clock_is_pll(void)
{
    return s_clock_is_pll;
}

void clock_init(void)
{
    s_clock_is_pll = clock_switch_to_pll_120mhz(); /* see above; falls back to IRC8M cleanly if HXTAL/PLL don't come up */

    /* GPIO ports + AFIO used by this Phase-1 bring-up (debug UART, YC1021
     * UART, EC25 UART + control pins, PWRHOLD, LEDs). RS485 (GPIOA pins
     * for USART1, GPIOD pins for the remapped USART2) is out of scope for
     * this phase and not enabled here. PD0/PD1 (would-be WDI) are
     * deliberately not used -- see the conflict note in board_config.h. */
    RCU->APB2EN |= RCU_APB2EN_AFEN
                 | RCU_APB2EN_PAEN  /* EC25 DTR (PA10) */
                 | RCU_APB2EN_PBEN  /* debug UART (PB6/7), EC25 PWRKEY (PB15), LEDs */
                 | RCU_APB2EN_PCEN  /* YC1021 UART (PC10/11), EC25 TX (PC12), PWRHOLD, LEDs */
                 | RCU_APB2EN_PDEN  /* YC1021 reset/enable (PD4/5), EC25 RX (PD2) */
                 | RCU_APB2EN_PEEN; /* EC25 modem power enable (PE2, VGSM LDO) -- confirmed via
                                      * the PandaBox master hardware note; this was NEVER enabled
                                      * before, so PE2 could never actually be driven regardless
                                      * of any GPIO write attempted on it -- likely why the modem
                                      * has shown zero response to every AT attempt so far. */

    /* Debug UART (USART0) is remapped PA9/PA10 -> PB6/PB7 on this board.
     * USART2's full remap (PB10/PB11 -> PD8/PD9, RS485_2) is applied here
     * too, unconditionally -- harmless even when RS485_2 isn't the
     * active TCS port (see tcs.c's TCS_USE_RS485_2 switch), since PD8/9
     * aren't used for anything else in this project. */
    AFIO->PCF0 |= DEBUG_UART_REMAP_BIT | RS485_2_REMAP_BIT;

    RCU->APB2EN |= RCU_APB2EN_USART0EN  /* debug console */
                 | RCU_APB2EN_SPI0EN;   /* SPI0 flash (GD25Q256E) -- see board_config.h's
                                          * UNCONFIRMED note on SPI0_FLASH_* pins */
    RCU->APB1EN |= RCU_APB1EN_UART3EN   /* YC1021 */
                 | RCU_APB1EN_UART4EN   /* EC25 */
                 | RCU_APB1EN_USART1EN  /* TCS meter bus, RS485_1 (PA2/PA3) */
                 | RCU_APB1EN_USART2EN; /* TCS meter bus, RS485_2 (PD8/PD9) -- alternate port for testing */
}
