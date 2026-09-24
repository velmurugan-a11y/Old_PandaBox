#ifndef CLOCK_H
#define CLOCK_H

/* Phase 2: switches SYSCLK to HXTAL(12MHz)->PLL->120MHz (see clock.c),
 * then enables peripheral clocks (GPIO ports + AFIO + the UARTs this
 * bring-up phase uses) and applies the one AFIO remap the debug UART
 * needs. */
void clock_init(void);

/* 1 if the HXTAL+PLL switch succeeded (SYSCLK genuinely running at
 * 120MHz, matching BOARD_SYSCLK_HZ); 0 if HXTAL or the PLL never
 * stabilized and the board fell back to running on IRC8M (8MHz) --
 * check this once logging is up, since clock_init() itself runs too
 * early to log anything. */
int clock_is_pll(void);

#endif /* CLOCK_H */
