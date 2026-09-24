/*
 * regs.h -- peripheral register layout for GD32F305VCT6 (LQFP100 -- the
 * exact populated part, confirmed from the XBOX_V2.5 schematic's own MCU
 * designator U100 and the BOM row "MCU,GD32F305VCT6,LQFP100"), register-
 * compatible with the STM32F1 high-density family (confirmed: GD32F305xx
 * datasheet's peripheral base addresses -- RCU=0x40021000,
 * GPIOA=0x40010800, USART0=0x40013800, etc. -- match STM32F103 exactly;
 * this project uses GigaDevice's own register names, STAT/DATA/BAUD/
 * CTL0-2/GP, rather than ST's SR/DR/BRR/CR1-3/GTPR, but the offsets and
 * bit meanings are the same peripheral design either way).
 *
 * All base addresses below are taken directly from GD32F305xx datasheet
 * Table 2-2 (memory map), not assumed/copied from an unrelated part.
 */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

/* ------------------------------------------------------------------ *
 * RCU (reset & clock unit) -- base 0x40021000
 * ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t CTL;      /* 0x00 */
    volatile uint32_t CFG0;     /* 0x04 */
    volatile uint32_t INTR;     /* 0x08 */
    volatile uint32_t APB2RST;  /* 0x0C */
    volatile uint32_t APB1RST;  /* 0x10 */
    volatile uint32_t AHBEN;    /* 0x14 */
    volatile uint32_t APB2EN;   /* 0x18 */
    volatile uint32_t APB1EN;   /* 0x1C */
} rcu_reg_t;
#define RCU  ((rcu_reg_t *)0x40021000u)

#define RCU_APB2EN_AFEN     (1u << 0)
#define RCU_APB2EN_PAEN     (1u << 2)
#define RCU_APB2EN_PBEN     (1u << 3)
#define RCU_APB2EN_PCEN     (1u << 4)
#define RCU_APB2EN_PDEN     (1u << 5)
#define RCU_APB2EN_PEEN     (1u << 6)
#define RCU_APB2EN_PFEN     (1u << 7)
#define RCU_APB2EN_PGEN     (1u << 8)
#define RCU_APB2EN_USART0EN (1u << 14)
#define RCU_APB2EN_SPI0EN   (1u << 12)

#define RCU_APB1EN_USART1EN (1u << 17)
#define RCU_APB1EN_USART2EN (1u << 18)
#define RCU_APB1EN_UART3EN  (1u << 19)
#define RCU_APB1EN_UART4EN  (1u << 20)

/* CTL (0x00): HXTAL/PLL enable + stability flags. */
#define RCU_CTL_HXTALEN     (1u << 16)
#define RCU_CTL_HXTALSTB    (1u << 17)
#define RCU_CTL_PLLEN       (1u << 24)
#define RCU_CTL_PLLSTB      (1u << 25)

/* CFG0 (0x04): system clock switch/status, prescalers, PLL config.
 * Bit layout confirmed against the GD32F30x datasheet (register-
 * compatible with STM32F103's RCC_CFGR) -- see regs.h header comment. */
#define RCU_CFG0_SCS_Msk        (0x3u << 0)
#define RCU_CFG0_SCS_IRC8M      (0x0u << 0)
#define RCU_CFG0_SCS_HXTAL      (0x1u << 0)
#define RCU_CFG0_SCS_PLL        (0x2u << 0)
#define RCU_CFG0_SCSS_Msk       (0x3u << 2)
#define RCU_CFG0_SCSS_IRC8M     (0x0u << 2)
#define RCU_CFG0_SCSS_HXTAL     (0x1u << 2)
#define RCU_CFG0_SCSS_PLL       (0x2u << 2)
#define RCU_CFG0_AHBPSC_Msk     (0xFu << 4)
#define RCU_CFG0_AHBPSC_DIV1    (0x0u << 4)
#define RCU_CFG0_APB1PSC_Msk    (0x7u << 8)
#define RCU_CFG0_APB1PSC_DIV1   (0x0u << 8)
#define RCU_CFG0_APB1PSC_DIV2   (0x4u << 8)
#define RCU_CFG0_APB2PSC_Msk    (0x7u << 11)
#define RCU_CFG0_APB2PSC_DIV1   (0x0u << 11)
#define RCU_CFG0_PLLSEL         (1u << 16)  /* 0 = IRC8M/2, 1 = HXTAL (optionally /2 via PLLPREDV below) */
#define RCU_CFG0_PLLPREDV       (1u << 17)  /* 0 = HXTAL not divided before PLL, 1 = HXTAL/2 */
#define RCU_CFG0_PLLMF_Msk      (0xFu << 18)
#define RCU_CFG0_PLLMF_MUL10    (0x8u << 18) /* (value+2) = multiplier for the 4-bit field, so 0x8 = x10 */

/* FMC (flash memory controller) -- base 0x40022000, register-compatible
 * with STM32F1's FLASH interface. Only the wait-state field is needed
 * here: flash can't keep up with reads at 120MHz without extra wait
 * states, and this MUST be raised before SYSCLK is actually switched to
 * the PLL, or the CPU can fetch garbage/hang. */
typedef struct {
    volatile uint32_t WS; /* 0x00 -- bits[2:0] = wait state count */
} fmc_reg_t;
#define FMC          ((fmc_reg_t *)0x40022000u)
#define FMC_WS_WSCNT_Msk (0x7u)
#define FMC_WS_WSCNT_3   (0x3u) /* per GD32F30x's own wait-state table for SYSCLK > 90MHz (up to 120MHz) */

/* ------------------------------------------------------------------ *
 * AFIO -- base 0x40010000. Two of this board's UARTs are pin-swapped via
 * remap bits here (confirmed from the schematic's net labels vs. the
 * datasheet's default pin table -- see board_config.h):
 *   - USART0 (GD)/USART1(ST) remap: PA9/PA10 -> PB6/PB7 (this board's
 *     debug/RS232 UART) -- AFIO_PCF0 bit 2.
 *   - USART2 (GD)/USART3(ST) FULL remap: PB10/PB11 -> PD8/PD9 (this
 *     board's RS485 port 2) -- AFIO_PCF0 bits [5:4] = 0b11.
 * ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t EC;    /* 0x00 -- event control */
    volatile uint32_t PCF0;  /* 0x04 -- AF remap & debug I/O config (was AFIO_MAPR) */
    volatile uint32_t EXTISS0; /* 0x08 */
    volatile uint32_t EXTISS1; /* 0x0C */
    volatile uint32_t EXTISS2; /* 0x10 */
    volatile uint32_t EXTISS3; /* 0x14 */
} afio_reg_t;
#define AFIO ((afio_reg_t *)0x40010000u)

#define AFIO_PCF0_USART0_REMAP        (1u << 2)  /* PA9/PA10 -> PB6/PB7 */
#define AFIO_PCF0_USART2_REMAP_FULL   (3u << 4)  /* PB10/PB11 -> PD8/PD9 */

/* ------------------------------------------------------------------ *
 * GPIO -- STM32F1-style CRL/CRH+IDR/ODR/BSRR/BRR, GD32 naming
 * ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t CTL0; /* 0x00 -- pin config, pins 0-7  (was CRL) */
    volatile uint32_t CTL1; /* 0x04 -- pin config, pins 8-15 (was CRH) */
    volatile uint32_t ISTAT;/* 0x08 -- input data  (was IDR) */
    volatile uint32_t OCTL; /* 0x0C -- output data (was ODR) */
    volatile uint32_t BOP;  /* 0x10 -- bit set/reset (was BSRR) */
    volatile uint32_t BC;   /* 0x14 -- bit clear (was BRR) */
    volatile uint32_t LOCK; /* 0x18 */
} gpio_reg_t;

#define GPIOA ((gpio_reg_t *)0x40010800u)
#define GPIOB ((gpio_reg_t *)0x40010C00u)
#define GPIOC ((gpio_reg_t *)0x40011000u)
#define GPIOD ((gpio_reg_t *)0x40011400u)
#define GPIOE ((gpio_reg_t *)0x40011800u)
#define GPIOF ((gpio_reg_t *)0x40011C00u)
#define GPIOG ((gpio_reg_t *)0x40012000u)

/* CTL0/CTL1: 4 bits per pin -- MODE[1:0] + CTL[1:0]. For alternate-
 * function push-pull output (needed for USART TX): MODE=0b11 (50MHz),
 * CTL=0b10. For floating/pull-up input (USART RX): MODE=0b00, CTL=0b01
 * (floating) or 0b10 (input w/ pull-up/down, needs OCTL set for pull-up). */
#define GPIO_MODE_AF_PP_50MHZ 0xBu /* 0b1011: MODE=11, CTL=10 */
#define GPIO_MODE_IN_FLOAT    0x4u /* 0b0100: MODE=00, CTL=01 */

/* ------------------------------------------------------------------ *
 * USART/UART -- GD32 naming: STAT/DATA/BAUD/CTL0/CTL1/CTL2/GP
 * ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t STAT; /* 0x00 */
    volatile uint32_t DATA; /* 0x04 */
    volatile uint32_t BAUD; /* 0x08 */
    volatile uint32_t CTL0; /* 0x0C */
    volatile uint32_t CTL1; /* 0x10 */
    volatile uint32_t CTL2; /* 0x14 */
    volatile uint32_t GP;   /* 0x18 */
} usart_reg_t;

#define USART0_BASE ((usart_reg_t *)0x40013800u)
#define USART1_BASE ((usart_reg_t *)0x40004400u)
#define USART2_BASE ((usart_reg_t *)0x40004800u)
#define UART3_BASE  ((usart_reg_t *)0x40004C00u)
#define UART4_BASE  ((usart_reg_t *)0x40005000u)

#define USART_STAT_TBE   (1u << 7) /* transmit buffer empty */
#define USART_STAT_RBNE  (1u << 5) /* read buffer not empty */
#define USART_STAT_ORERR (1u << 3) /* overrun error -- cleared by reading STAT then DATA */

#define USART_CTL0_UEN    (1u << 13) /* USART enable */
#define USART_CTL0_RBNEIE (1u << 5)  /* RBNE interrupt enable */
#define USART_CTL0_TEN    (1u << 3)  /* transmitter enable */
#define USART_CTL0_REN    (1u << 2)  /* receiver enable */

/* ------------------------------------------------------------------ *
 * SPI0 -- base 0x40013000 (APB2), register-compatible with STM32F1's
 * SPI1 (same family-wide compatibility this file's header comment
 * already establishes for every other peripheral).
 * ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t CTL0;    /* 0x00 */
    volatile uint32_t CTL1;    /* 0x04 */
    volatile uint32_t STAT;    /* 0x08 */
    volatile uint32_t DATA;    /* 0x0C */
    volatile uint32_t CRCPOLY; /* 0x10 */
    volatile uint32_t RCRC;    /* 0x14 */
    volatile uint32_t TCRC;    /* 0x18 */
    volatile uint32_t I2SCTL;  /* 0x1C */
    volatile uint32_t I2SPSC;  /* 0x20 */
} spi_reg_t;
#define SPI0_BASE ((spi_reg_t *)0x40013000u)

#define SPI_CTL0_CPHA     (1u << 0)
#define SPI_CTL0_CPOL     (1u << 1)
#define SPI_CTL0_MSTMOD   (1u << 2)
#define SPI_CTL0_PSC_DIV32 (0x4u << 3) /* PCLK2/32 -- conservative bring-up speed */
#define SPI_CTL0_SPIEN    (1u << 6)
#define SPI_CTL0_LF       (0u << 7) /* MSB first (0 = MSB, matches reset default) */
#define SPI_CTL0_SWNSSEN  (1u << 9)  /* software NSS management */
#define SPI_CTL0_SWNSS    (1u << 8)  /* internal NSS level when SWNSSEN=1 */

#define SPI_STAT_RBNE     (1u << 0)
#define SPI_STAT_TBE      (1u << 1)

/* ------------------------------------------------------------------ *
 * NVIC (Cortex-M4 core peripheral, NOT vendor-specific).
 * ------------------------------------------------------------------ */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100u)
#define UART3_IRQN 52u
#define UART4_IRQN 53u

static inline void nvic_enable_irq(uint32_t irqn)
{
    NVIC_ISER[irqn / 32u] = (1u << (irqn % 32u));
}

/* ------------------------------------------------------------------ *
 * SCB (Cortex-M4 core peripheral, NOT vendor-specific) -- base
 * 0xE000ED00, same on every Cortex-M3/M4 part. Used only for AIRCR's
 * SYSRESETREQ bit (software system reset).
 * ------------------------------------------------------------------ */
#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0Cu)
#define SCB_AIRCR_VECTKEY   (0x5FAu << 16)
#define SCB_AIRCR_SYSRESETREQ (1u << 2)

/* Needed for FreeRTOS bring-up: ICSR (PendSV/SysTick pend-set bits) and
 * SHPR2/SHPR3 (settable priority for PendSV/SysTick/SVCall, part of the
 * standard Cortex-M system-handler priority block at 0xE000ED18). */
#define SCB_ICSR  (*(volatile uint32_t *)0xE000ED04u)
#define SCB_SHPR2 (*(volatile uint32_t *)0xE000ED1Cu)
#define SCB_SHPR3 (*(volatile uint32_t *)0xE000ED20u)

#endif /* REGS_H */
