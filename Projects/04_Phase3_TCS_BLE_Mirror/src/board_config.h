/*
 * BRINGUP_MODULE -- when set to BRINGUP_ONLY_BLE or BRINGUP_ONLY_TELIT,
 * main.c creates only that one task (Meter-with-just-YC1021, or Telit-
 * with-EC25-AT-handshake-and-GPS) and skips everything else (LED test,
 * TCS, the other tasks) so each build-flash-RTT cycle is fast and the
 * log only has that one module's lines in it. BRINGUP_NONE restores
 * full normal operation (all tasks, LED test, TCS scan). Switch by
 * editing the value below, or override at build time with e.g.
 * -DBRINGUP_MODULE=BRINGUP_ONLY_TELIT.
 */
#define BRINGUP_NONE        0
#define BRINGUP_ONLY_BLE    1
#define BRINGUP_ONLY_TELIT  2

#ifndef BRINGUP_MODULE
#define BRINGUP_MODULE BRINGUP_ONLY_BLE
#endif

/*
 * board_config.h -- every board-specific fact lives here. Values below are
 * taken directly from the XBOX_V2.5 schematic + BOM (not guessed) unless
 * marked UNCONFIRMED.
 *
 * Confirmed hardware (schematic U100 designator + BOM row match exactly):
 *   - MCU: GD32F305VCT6, LQFP100 (NOT the "Z"/LQFP144 package originally
 *     mentioned -- the schematic and BOM both say VCT6).
 *   - MCU crystal (HXTAL, X101): 12MHz, +/-10ppm, 20pF load.
 *   - YC1021 (U5, QFN32): Bluetooth 3.0 BR + BLE 5.0. Its own crystal
 *     (X2): 24MHz, matching the datasheet's recommended default exactly.
 *   - EC25 (U1): exact variant EC25AFA-512-STD (EC25-AF, North America/
 *     FirstNet bands).
 *   - There IS a separate real WiFi module on this board -- U801,
 *     FC20N-Q93, SDIO-attached, own antenna (ANT_WIFI). NOT covered by
 *     this project yet (SDIO driver is separate, larger work) -- do not
 *     assume "no WiFi" the way the old reconstructed X-Box firmware's
 *     source tree does; that assumption does not hold for this board.
 *   - The BT RF amplifier populated is GSR2401 (U10, QFN16), not the
 *     RF5745 referenced by an earlier datasheet -- not yet wired into
 *     this project (no control lines identified for it yet).
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "regs.h" /* gpio_reg_t/usart_reg_t types and AFIO_PCF0_* bits used below */

/* ------------------------------------------------------------------ *
 * Clock configuration
 * ------------------------------------------------------------------ */
/* Phase 2 (see clock.c): HXTAL(12MHz, X101) -> PLL x10 -> 120MHz SYSCLK,
 * per the PandaBox master hardware note's explicit rule 1 ("HXTAL_VALUE
 * = 12000000 and the PLL set for 12MHz -> 120MHz"). AHB=/1 (120MHz),
 * APB1=/2 (60MHz, its documented max on this family), APB2=/1 (120MHz)
 * -- clock_init() falls back to IRC8M (8MHz) if HXTAL/PLL never
 * stabilize; check clock_is_pll() once logging is up to confirm which
 * one is actually running before trusting these values. */
#define BOARD_HXTAL_HZ    12000000u
#define BOARD_SYSCLK_HZ   120000000u
#define BOARD_PCLK1_HZ    (BOARD_SYSCLK_HZ / 2u)
#define BOARD_PCLK2_HZ    BOARD_SYSCLK_HZ

/* ------------------------------------------------------------------ *
 * Debug / RS232 console -- UART0 (GD naming) = USART0 peripheral,
 * REMAPPED to PB6(TX)/PB7(RX) (schematic net PB6_UART0_TX/PB7_UART0_RX),
 * routed through U9 (BL13232ETS RS232 driver) to the DB9 connector J3.
 * Default (non-remapped) pins would be PA9/PA10 -- this board does NOT
 * use those for USART0.
 * ------------------------------------------------------------------ */
#define DEBUG_UART_BASE       USART0_BASE
#define DEBUG_UART_BAUD       115200u
#define DEBUG_UART_GPIO_PORT  GPIOB
#define DEBUG_UART_TX_PIN     6
#define DEBUG_UART_RX_PIN     7
#define DEBUG_UART_REMAP_BIT  AFIO_PCF0_USART0_REMAP

/* ------------------------------------------------------------------ *
 * RS485 port 1 (DB25-1, J1) -- TCS/LCR meter node bus. UART1 (GD
 * naming) = USART1 peripheral, DEFAULT pins PA2(TX)/PA3(RX), through
 * U104 (SIT3088EESA RS485 transceiver) -- this specific board/cable is
 * wired for RS232 point-to-point rather than true RS485 multidrop, per
 * direct confirmation, but that's external wiring only; the MCU-side
 * UART config is identical either way.
 *
 * Baud CONFIRMED (not guessed) from the real UART_TO_METER_1 v3.01
 * release's FSP project configuration (configuration.xml: g_uart2/
 * g_uart3 both set to 19200, modulation enabled, max error 1%) -- see
 * src/tcs.c for the full protocol port from that same release.
 * ------------------------------------------------------------------ */
#define RS485_1_UART_BASE     USART1_BASE
#define RS485_1_GPIO_PORT     GPIOA
#define RS485_1_TX_PIN        2
#define RS485_1_RX_PIN        3
#define RS485_1_UART_BAUD     19200u
/* no remap needed -- default pin location */

/* ------------------------------------------------------------------ *
 * RS485 port 2 (DB25-2, J2) -- second LCR/meter node bus. UART2 (GD
 * naming) = USART2 peripheral, FULL REMAP to PD8(TX)/PD9(RX), through
 * U4 (SIT3088EESA). Out of scope for this Phase-1 bring-up.
 * ------------------------------------------------------------------ */
#define RS485_2_UART_BASE     USART2_BASE
#define RS485_2_GPIO_PORT     GPIOD
#define RS485_2_TX_PIN        8
#define RS485_2_RX_PIN        9
#define RS485_2_REMAP_BIT     AFIO_PCF0_USART2_REMAP_FULL

/* ------------------------------------------------------------------ *
 * YC1021 (BT/BLE, U5) -- UART3 (GD naming), DEFAULT pins PC10(TX)/
 * PC11(RX), no remap needed. Confirmed control lines:
 *   - PD4 = BT_RST (drives YC1021 pin 10, RESET, active low per
 *     datasheet: "Gloable reset, active low")
 *   - PD5 = BT_EN (enables the BT_3.3V rail feeding the YC1021, via U6
 *     RT9080-33GJ5 LDO's CE pin)
 * RTS/CTS hardware flow control for the HCI-H5 link is NOT wired up in
 * this Phase-1 driver (see src/yc1021.c) -- no RTS/CTS net was
 * identified for this UART in the schematic extraction; confirm before
 * assuming software-only framing is sufficient at full throughput.
 * ------------------------------------------------------------------ */
#define YC1021_UART_BASE      UART3_BASE
#define YC1021_UART_BAUD      115200u /* YC1021 HCI-H5 default; the datasheet lists up to 3.25Mbps, confirm provisioned baud before raising this */
#define YC1021_GPIO_PORT      GPIOC
#define YC1021_TX_PIN         10
#define YC1021_RX_PIN         11

#define YC1021_RESET_PORT     GPIOD
#define YC1021_RESET_PIN      4   /* PD4_BT_RST, active LOW */
#define YC1021_ENABLE_PORT    GPIOD
#define YC1021_ENABLE_PIN     5   /* PD5_BT_EN, drives BT_3.3V rail enable */

/* ------------------------------------------------------------------ *
 * Quectel EC25 (4G, U1, exact part EC25AFA-512-STD) -- UART4 (GD
 * naming), DEFAULT pins PC12(TX)/PD2(RX), no remap needed. Confirmed
 * control lines:
 *   - PB15 = GSM_PWRKEY (drives EC25 pin 21, PWRKEY -- per Quectel's
 *     hardware design guide this needs a low pulse of a few hundred ms
 *     to a couple seconds to power the module on; exact pulse width for
 *     THIS board not confirmed from the schematic alone, use Quectel's
 *     documented minimum and verify against STATUS/power draw)
 *   - PA10 = GSM_DTR (EC25 pin 66, DTR)
 * NOT confirmed from this schematic pass: which pin (if any) reads back
 * EC25's STATUS (pin 61) or drives RESET_N (pin 20). Treat power-on as
 * PWRKEY-pulse-only until/unless those are confirmed.
 * ------------------------------------------------------------------ */
#define EC25_UART_BASE        UART4_BASE
#define EC25_UART_BAUD        115200u
#define EC25_GPIO_PORT        GPIOC
#define EC25_TX_PIN           12
#define EC25_RX_PORT          GPIOD
#define EC25_RX_PIN           2

#define EC25_PWRKEY_PORT      GPIOB
#define EC25_PWRKEY_PIN       15  /* PB15_GSM_PWRKEY */
#define EC25_DTR_PORT         GPIOA
#define EC25_DTR_PIN          10  /* PA10_GSM_DTR */

/* Confirmed via the PandaBox master hardware note (Leo's PCB+BOM
 * cross-check): PE2 enables U401, the modem's own VGSM LDO supply.
 * Documented power-on sequence is PE2=1, wait for the supply to
 * settle, THEN pulse PWRKEY -- this pin was never defined or driven at
 * all before, meaning every previous AT/PWRKEY attempt this session
 * was against an unpowered modem. VGSM's actual output voltage
 * (4.0V vs. a documented-damaging 5.3V) was flagged as the #1 item to
 * verify before enabling this -- confirmed safe (~4.0V) before this
 * was wired up. */
#define EC25_POWER_PORT       GPIOE
#define EC25_POWER_PIN        2   /* PE2, VGSM LDO enable, active HIGH */

/* ------------------------------------------------------------------ *
 * Board power/watchdog housekeeping:
 *   - PC8 = MCU_PWRHOLD -- must be driven HIGH early, or the board's own
 *     power-latch circuit may cut power shortly after boot. No conflict
 *     found for this pin; used as-is.
 *   - PD0/PD1 = WDI -- CONFLICT FOUND, DELIBERATELY NOT WIRED UP. The
 *     schematic's own MCU pin list (page 1) shows pins 12/13 as
 *     "OSC_IN/PD0" and "OSC_OUT/PD1" -- the SAME physical pins -- with
 *     the confirmed-populated 12MHz crystal (X101, BOM row 37) wired
 *     directly onto them. Page 2's net-label block separately lists
 *     "PD0_WDI"/"PD1_WDI", implying those pins ALSO drive an external
 *     watchdog supervisor input. A pin cannot be both an analog crystal
 *     oscillator input and a digital GPIO simultaneously -- this is
 *     either a stale label from an earlier board revision, or those WDI
 *     nets belong to something else (there's a separate small PIC10F200
 *     housekeeping IC on page 2 that may be the real driver). Do not
 *     configure PD0/PD1 as GPIO until this is resolved by visually
 *     inspecting the schematic (not text-extracted) or asking whoever
 *     laid out the board -- misconfiguring an active crystal pin as a
 *     digital output would break HXTAL for the Phase-2 clock config even
 *     though Phase 1 doesn't depend on it. If a real external watchdog
 *     supervisor exists on this board, find out what actually feeds its
 *     WDI pin before assuming "no code kicks it" is safe either.
 * ------------------------------------------------------------------ */
#define PWRHOLD_PORT          GPIOC
#define PWRHOLD_PIN           8

/* WDI0/WDI1 deliberately not defined -- see conflict note above. */

/* ------------------------------------------------------------------ *
 * SPI0 flash (GD25Q256E, 32MB NOR) -- UNCONFIRMED. No schematic/BOM file
 * exists in this repo for this chip's pin assignment (unlike every other
 * block in this file, which is sourced from the real XBOX_V2.5 schematic).
 * These are GD32F305's default (non-remapped) SPI0 pins, used as a
 * starting guess -- weak corroborating precedent: the sibling STM32F103
 * board's GD25Q driver (different MCU/schematic) also puts flash CS on
 * PA4. DO NOT trust this until flash_store's boot-time JEDEC ID readback
 * (expect GigaDevice mfr ID 0xC8) confirms it on real hardware -- garbage/
 * 0xFF back means these pins are wrong and need revisiting against the
 * real schematic.
 * ------------------------------------------------------------------ */
#define SPI0_FLASH_GPIO_PORT  GPIOA
#define SPI0_FLASH_CS_PIN     4   /* UNCONFIRMED -- software-controlled CS, not hardware NSS */
#define SPI0_FLASH_SCK_PIN    5   /* UNCONFIRMED */
#define SPI0_FLASH_MISO_PIN   6   /* UNCONFIRMED */
#define SPI0_FLASH_MOSI_PIN   7   /* UNCONFIRMED */

/* ------------------------------------------------------------------ *
 * LEDs (all confirmed net names from the schematic)
 * ------------------------------------------------------------------ */
#define LED_GPS_GREEN_PORT    GPIOB
#define LED_GPS_GREEN_PIN     0
#define LED_PWR_RED_PORT      GPIOB
#define LED_PWR_RED_PIN       1
#define LED_WIFI_ORANGE_PORT  GPIOC
#define LED_WIFI_ORANGE_PIN   4
#define LED_BT_BLUE_PORT      GPIOC
#define LED_BT_BLUE_PIN       5
#define LED_LCP1_PORT         GPIOC
#define LED_LCP1_PIN          6
#define LED_LCP2_PORT         GPIOC
#define LED_LCP2_PIN          7

#endif /* BOARD_CONFIG_H */
