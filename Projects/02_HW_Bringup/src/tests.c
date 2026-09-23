/*!
    \file    tests.c
    \brief   bring-up test commands (see docs/PandaBox_Peripheral_Bringup.md section 2)

    Nothing here erases or writes the external SPI flash: it holds Leo's delivery history.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "uart.h"
#include "tests.h"

/* ------------------------------------------------------------------ helpers */

static int arg_count;
static char *args[8];

static void split_args(char *line)
{
    char *p = line;

    arg_count = 0;
    while(*p && arg_count < 8) {
        while(*p == ' ') {
            *p++ = '\0';
        }
        if(*p == '\0') {
            break;
        }
        args[arg_count++] = p;
        while(*p && *p != ' ') {
            p++;
        }
    }
}

static uint32_t arg_u32(int i, uint32_t def)
{
    return (i < arg_count) ? strtoul(args[i], NULL, 0) : def;
}

static void hexdump_line(const char *tag, uint32_t addr, const uint8_t *d, uint32_t n)
{
    uint32_t i;

    printf("%s %08lX", tag, (unsigned long)addr);
    for(i = 0U; i < n; i++) {
        printf(" %02X", d[i]);
    }
    printf("\n");
}

/* print everything received on a port for up to ms, as hex and as text */
static uint32_t dump_rx(uart_port_t port, const char *tag, uint32_t ms, uint32_t idle_ms)
{
    uint8_t buf[32];
    uint32_t n = 0U, total = 0U, start = millis(), last = millis();
    int c;

    while((millis() - start) < ms) {
        c = uart_getc(port);
        if(c >= 0) {
            buf[n++] = (uint8_t)c;
            total++;
            last = millis();
            if(n == sizeof(buf)) {
                hexdump_line(tag, total - n, buf, n);
                n = 0U;
            }
        } else if(idle_ms && total && (millis() - last) > idle_ms) {
            break;
        }
    }
    if(n) {
        hexdump_line(tag, total - n, buf, n);
    }
    return total;
}

static uint8_t pin_level(uint32_t port, uint32_t pin)
{
    return (SET == gpio_input_bit_get(port, pin)) ? 1U : 0U;
}

/* ------------------------------------------------------------------ info */

static void cmd_info(void)
{
    uint32_t f = boot_reset_flags();

    SystemCoreClockUpdate();
    printf("SystemCoreClock = %lu Hz\n", (unsigned long)SystemCoreClock);
    printf("RCU_CTL  = %08lX (HXTALSTB=%lu PLLSTB=%lu)\n", (unsigned long)RCU_CTL,
           (unsigned long)((RCU_CTL >> 17) & 1U), (unsigned long)((RCU_CTL >> 25) & 1U));
    printf("RCU_CFG0 = %08lX  RCU_CFG1 = %08lX  SCSS=%lu\n", (unsigned long)RCU_CFG0,
           (unsigned long)RCU_CFG1, (unsigned long)((RCU_CFG0 >> 2) & 3U));
    printf("Flash size = %u KB, UID = %08lX %08lX %08lX\n", *(volatile uint16_t *)0x1FFFF7E0U,
           (unsigned long)*(volatile uint32_t *)0x1FFFF7E8U, (unsigned long)*(volatile uint32_t *)0x1FFFF7ECU,
           (unsigned long)*(volatile uint32_t *)0x1FFFF7F0U);
    printf("Reset flags = %08lX:%s%s%s%s%s\n", (unsigned long)f,
           (f & BIT(26)) ? " PIN" : "", (f & BIT(27)) ? " POWER" : "", (f & BIT(28)) ? " SOFTWARE" : "",
           (f & BIT(29)) ? " FWDGT" : "", (f & BIT(31)) ? " LOWPOWER" : "");
    printf("RESULT clock %s SystemCoreClock=%lu HXTAL=%s\n",
           (SystemCoreClock == 120000000U && (RCU_CTL & RCU_CTL_HXTALSTB)) ? "PASS" : "FAIL",
           (unsigned long)SystemCoreClock, (RCU_CTL & RCU_CTL_HXTALSTB) ? "12MHz" : "FAILED");
}

/* ------------------------------------------------------------------ LEDs */

typedef struct { uint32_t port; uint32_t pin; const char *name; } led_t;
static const led_t leds[] = {
    {LED_PWR_PORT, LED_PWR_PIN, "PWR"}, {LED_GPS_PORT, LED_GPS_PIN, "GPS"},
    {LED_WIFI_PORT, LED_WIFI_PIN, "WIFI"}, {LED_BT_PORT, LED_BT_PIN, "BT"},
    {LED_LCP1_PORT, LED_LCP1_PIN, "LCP1"}, {LED_LCP2_PORT, LED_LCP2_PIN, "LCP2"},
};

static void cmd_led(void)
{
    uint32_t i, n = arg_u32(1, 99U), on = arg_u32(2, 1U);

    if(n == 99U) {
        for(i = 0U; i < 6U * 3U; i++) {
            gpio_bit_write(leds[i % 6U].port, leds[i % 6U].pin, SET);
            delay_ms(200U);
            gpio_bit_write(leds[i % 6U].port, leds[i % 6U].pin, RESET);
        }
        printf("RESULT led INFO chase done\n");
        return;
    }
    if(n < 6U) {
        gpio_bit_write(leds[n].port, leds[n].pin, on ? SET : RESET);
        printf("RESULT led INFO %s=%lu\n", leds[n].name, (unsigned long)on);
    }
}

/* ------------------------------------------------------------------ ADC */

static void adc_setup(void)
{
    static uint8_t done = 0U;

    if(done) {
        return;
    }
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV8);    /* 15 MHz */
    gpio_init(ADC_12V_PORT, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, ADC_12V_PIN);
    gpio_init(ADC_CELL_PORT, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, ADC_CELL_PIN);
    adc_deinit(ADC0);
    adc_mode_config(ADC_MODE_FREE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 1U);
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC0_1_2_EXTTRIG_ROUTINE_NONE);
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, ENABLE);
    adc_enable(ADC0);
    delay_ms(2U);
    adc_calibration_enable(ADC0);
    done = 1U;
}

/* returns millivolts at the pin (VREF+ = 3.3 V), 16-sample average */
static uint32_t adc_read_mv(uint8_t ch)
{
    uint32_t i, sum = 0U;

    adc_setup();
    adc_routine_channel_config(ADC0, 0U, ch, ADC_SAMPLETIME_239POINT5);
    for(i = 0U; i < 16U; i++) {
        adc_flag_clear(ADC0, ADC_FLAG_EOC);
        adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
        while(RESET == adc_flag_get(ADC0, ADC_FLAG_EOC)) {
        }
        sum += adc_routine_data_read(ADC0);
    }
    return (sum / 16U) * 3300U / 4095U;
}

static void cmd_adc(void)
{
    uint32_t v12, vcell;

    v12 = adc_read_mv(ADC_12V_CH) * 11U;
    gpio_bit_set(BATT_EN_PORT, BATT_EN_PIN);
    delay_ms(20U);
    vcell = adc_read_mv(ADC_CELL_CH) * 2U;
    gpio_bit_reset(BATT_EN_PORT, BATT_EN_PIN);
    printf("V_12V = %lu mV (PC0 x11), V_coin_cell = %lu mV (PC1 x2, PE1 on for 20 ms)\n",
           (unsigned long)v12, (unsigned long)vcell);
    printf("RESULT adc INFO v12_mv=%lu vcell_mv=%lu\n", (unsigned long)v12, (unsigned long)vcell);
}

/* ------------------------------------------------------------------ inputs */

static void cmd_inputs(void)
{
    uint8_t l1 = pin_level(DET_LCP1_12V_PORT, DET_LCP1_12V_PIN);
    uint8_t l2 = pin_level(DET_LCP2_12V_PORT, DET_LCP2_12V_PIN);
    uint8_t db = pin_level(DET_DB9_12V_PORT, DET_DB9_12V_PIN);
    uint8_t vb = pin_level(DET_VBUS_PORT, DET_VBUS_PIN);

    printf("PB13 LCR1 pin13 12V: %u (%s)\n", l1, l1 ? "absent" : "PRESENT");
    printf("PB12 LCR2 pin13 12V: %u (%s)\n", l2, l2 ? "absent" : "PRESENT");
    printf("PE11 DB9 pin8  12V : %u (%s)\n", db, db ? "absent" : "PRESENT");
    printf("PA9  USB-C VBUS    : %u (%s)\n", vb, vb ? "PRESENT" : "absent");
    printf("RESULT inputs INFO lcr1_12v=%u lcr2_12v=%u db9_12v=%u vbus=%u\n", !l1, !l2, !db, vb);
}

/* ------------------------------------------------------------------ RTC (read only) */

static void cmd_rtc(void)
{
    uint32_t bd, cnt = 0U;

    rcu_periph_clock_enable(RCU_PMU);
    rcu_periph_clock_enable(RCU_BKPI);
    bd = RCU_BDCTL;
    printf("RCU_BDCTL = %08lX: LXTALEN=%lu LXTALSTB=%lu RTCSRC=%lu RTCEN=%lu\n", (unsigned long)bd,
           (unsigned long)(bd & 1U), (unsigned long)((bd >> 1) & 1U), (unsigned long)((bd >> 8) & 3U),
           (unsigned long)((bd >> 15) & 1U));
    if(bd & RCU_BDCTL_RTCEN) {
        rtc_register_sync_wait();
        cnt = rtc_counter_get();
        delay_ms(1100U);
        printf("RTC counter = %lu, after 1.1 s = %lu (unix epoch if set by the app)\n",
               (unsigned long)cnt, (unsigned long)rtc_counter_get());
    }
    printf("RESULT rtc %s lxtal=%lu rtcen=%lu counter=%lu\n",
           ((bd & RCU_BDCTL_LXTALSTB) && (bd & RCU_BDCTL_RTCEN)) ? "PASS" : "INFO",
           (unsigned long)((bd >> 1) & 1U), (unsigned long)((bd >> 15) & 1U), (unsigned long)cnt);
}

/* ------------------------------------------------------------------ LCR transceivers */

static const struct {
    uart_port_t port;
    uint32_t rx_port, rx_pin, dir_port, dir_pin;
} lcr[2] = {
    {PORT_LCR1, LCR1_RX_PORT, LCR1_RX_PIN, RS485_DIR1_PORT, RS485_DIR1_PIN},
    {PORT_LCR2, LCR2_RX_PORT, LCR2_RX_PIN, RS485_DIR2_PORT, RS485_DIR2_PIN},
};

static void lcr_all_off(void)
{
    gpio_bit_reset(RS232_EN_PORT, RS232_EN_PIN);
    gpio_bit_reset(RS485_EN_PORT, RS485_EN_PIN);
    gpio_bit_set(RS485_DIR1_PORT, RS485_DIR1_PIN);
    gpio_bit_set(RS485_DIR2_PORT, RS485_DIR2_PIN);
}

/* RX pin read as GPIO input with internal pull-down: a powered transceiver output drives it high (idle) */
static uint8_t lcr_rx_level(int p)
{
    uart_deinit(lcr[p].port);
    gpio_init(lcr[p].rx_port, GPIO_MODE_IPD, GPIO_OSPEED_2MHZ, lcr[p].rx_pin);
    delay_ms(5U);
    return pin_level(lcr[p].rx_port, lcr[p].rx_pin);
}

/* send a pattern and count how much comes back (needs a loopback plug / cable) */
static uint32_t lcr_loop(uart_port_t tx, uart_port_t rx, uint32_t baud, uint8_t rs485, int txp)
{
    static const char pat[] = "PANDA-LOOP-0123456789";
    uint32_t n = 0U, start;
    int c;

    uart_init(tx, baud);
    if(rx != tx) {
        uart_init(rx, baud);
    }
    delay_ms(5U);
    uart_flush_rx(rx);
    if(rs485) {
        gpio_bit_reset(lcr[txp].dir_port, lcr[txp].dir_pin);     /* transmit */
    }
    uart_write(tx, (const uint8_t *)pat, sizeof(pat) - 1U);
    uart_wait_tx_done(tx);
    if(rs485) {
        gpio_bit_set(lcr[txp].dir_port, lcr[txp].dir_pin);       /* back to receive */
    }
    start = millis();
    while((millis() - start) < 50U) {
        c = uart_getc(rx);
        if(c >= 0 && n < sizeof(pat) - 1U && (char)c == pat[n]) {
            n++;
        }
    }
    return n;
}

static void cmd_rs232(void)
{
    int p = (int)arg_u32(1, 1U) - 1;
    uint8_t off, on;
    uint32_t mv_off = 0U, mv_on = 0U, echo;

    if(p < 0 || p > 1) {
        return;
    }
    lcr_all_off();
    delay_ms(20U);
    off = lcr_rx_level(p);
    if(p == 0) {
        gpio_init(LCR1_RX_PORT, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, LCR1_RX_PIN);
        mv_off = adc_read_mv(ADC_CHANNEL_3);   /* PA3 = ADC012_IN3 */
    }
    gpio_bit_set(RS232_EN_PORT, RS232_EN_PIN);
    delay_ms(50U);                             /* charge pump start-up */
    on = lcr_rx_level(p);
    if(p == 0) {
        gpio_init(LCR1_RX_PORT, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, LCR1_RX_PIN);
        mv_on = adc_read_mv(ADC_CHANNEL_3);
    }
    echo = lcr_loop(lcr[p].port, lcr[p].port, 19200U, 0U, p);
    lcr_all_off();
    uart_deinit(lcr[p].port);

    printf("RS232 port %d: RX idle level rail OFF=%u, rail ON=%u", p + 1, off, on);
    if(p == 0) {
        printf(" (PA3 %lu mV -> %lu mV)", (unsigned long)mv_off, (unsigned long)mv_on);
    }
    printf("\nLoopback (DB25 pin14<->15) echoed %lu/21 bytes at 19200\n", (unsigned long)echo);
    printf("RESULT rs232_%d %s rx_off=%u rx_on=%u loop=%lu/21\n", p + 1,
           (on == 1U && off == 0U) ? "PASS" : "FAIL", off, on, (unsigned long)echo);
}

static void cmd_rs485(void)
{
    int p = (int)arg_u32(1, 1U) - 1;
    uint8_t off, rxmode, txmode;
    uint32_t cross;

    if(p < 0 || p > 1) {
        return;
    }
    lcr_all_off();
    delay_ms(20U);
    off = lcr_rx_level(p);
    gpio_bit_set(RS485_EN_PORT, RS485_EN_PIN);         /* direction already = receive */
    delay_ms(20U);
    rxmode = lcr_rx_level(p);                          /* RO driven by fail-safe bias (A pulled up, B down) */
    gpio_bit_reset(lcr[p].dir_port, lcr[p].dir_pin);   /* transmit: RO goes high-Z */
    delay_ms(2U);
    txmode = pin_level(lcr[p].rx_port, lcr[p].rx_pin);
    gpio_bit_set(lcr[p].dir_port, lcr[p].dir_pin);
    /* port1 -> port2 only works with a J1-J2 cable (pins 14-14, 15-15) */
    cross = lcr_loop(lcr[p].port, lcr[1 - p].port, 19200U, 1U, p);
    lcr_all_off();
    uart_deinit(PORT_LCR1);
    uart_deinit(PORT_LCR2);

    printf("RS485 port %d: RO level rail OFF=%u, rail ON+receive=%u, rail ON+transmit(RO off)=%u\n",
           p + 1, off, rxmode, txmode);
    printf("Cross test port %d -> port %d (needs J1-J2 cable) received %lu/21 bytes\n", p + 1, 2 - p,
           (unsigned long)cross);
    printf("RESULT rs485_%d %s rx_off=%u rx_on=%u tx_mode=%u cross=%lu/21\n", p + 1,
           (rxmode == 1U && txmode == 0U) ? "PASS" : "FAIL", off, rxmode, txmode, (unsigned long)cross);
}

/* PE5/PE6 in all four combinations; RX pins read with pull-down, PA3 also as a voltage */
static void cmd_rsdiag(void)
{
    static const char *name[4] = {"both OFF", "RS232 only (PE5)", "RS485 only (PE6)", "both ON"};
    uint32_t combo, settle = arg_u32(1, 300U), mv;
    uint8_t d1, d2;

    for(combo = 0U; combo < 4U; combo++) {
        lcr_all_off();
        delay_ms(settle);
        if(combo & 1U) {
            gpio_bit_set(RS232_EN_PORT, RS232_EN_PIN);
        }
        if(combo & 2U) {
            gpio_bit_set(RS485_EN_PORT, RS485_EN_PIN);
        }
        delay_ms(settle);
        d1 = lcr_rx_level(0);
        d2 = lcr_rx_level(1);
        gpio_init(LCR1_RX_PORT, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, LCR1_RX_PIN);
        mv = adc_read_mv(ADC_CHANNEL_3);
        printf("%-18s PE5=%lu PE6=%lu : PA3(RX1)=%u PD9(RX2)=%u  PA3=%lu mV\n", name[combo],
               (unsigned long)(combo & 1U), (unsigned long)((combo >> 1) & 1U), d1, d2, (unsigned long)mv);
        printf("RESULT rsdiag INFO combo=%lu pe5=%lu pe6=%lu rx1=%u rx2=%u pa3_mv=%lu\n", (unsigned long)combo,
               (unsigned long)(combo & 1U), (unsigned long)((combo >> 1) & 1U), d1, d2, (unsigned long)mv);
    }
    lcr_all_off();
}

/* ------------------------------------------------------------------ GD25Q256E (read only) */

static void spi_flash_setup(uint32_t psc)
{
    spi_parameter_struct s;

    rcu_periph_clock_enable(RCU_SPI0);
    gpio_init(FLASH_SPI_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, FLASH_SCK_PIN | FLASH_MOSI_PIN);
    gpio_init(FLASH_SPI_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, FLASH_MISO_PIN);
    spi_i2s_deinit(FLASH_SPI);
    spi_struct_para_init(&s);
    s.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    s.device_mode = SPI_MASTER;
    s.frame_size = SPI_FRAMESIZE_8BIT;
    s.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    s.nss = SPI_NSS_SOFT;
    s.prescale = psc;
    s.endian = SPI_ENDIAN_MSB;
    spi_init(FLASH_SPI, &s);
    spi_enable(FLASH_SPI);
}

static uint8_t spi_xfer(uint8_t b)
{
    while(RESET == spi_i2s_flag_get(FLASH_SPI, SPI_FLAG_TBE)) {
    }
    spi_i2s_data_transmit(FLASH_SPI, b);
    while(RESET == spi_i2s_flag_get(FLASH_SPI, SPI_FLAG_RBNE)) {
    }
    return (uint8_t)spi_i2s_data_receive(FLASH_SPI);
}

#define CS_LOW()    gpio_bit_reset(FLASH_CS_PORT, FLASH_CS_PIN)
#define CS_HIGH()   gpio_bit_set(FLASH_CS_PORT, FLASH_CS_PIN)

static void flash_cmd_read(uint8_t cmd, uint8_t *out, uint32_t n)
{
    CS_LOW();
    spi_xfer(cmd);
    while(n--) {
        *out++ = spi_xfer(0xFFU);
    }
    CS_HIGH();
}

/* 13h: read with 4-byte address, works in either address mode, no state change */
static void flash_read(uint32_t addr, uint8_t *out, uint32_t n)
{
    CS_LOW();
    spi_xfer(0x13U);
    spi_xfer((uint8_t)(addr >> 24));
    spi_xfer((uint8_t)(addr >> 16));
    spi_xfer((uint8_t)(addr >> 8));
    spi_xfer((uint8_t)addr);
    while(n--) {
        *out++ = spi_xfer(0xFFU);
    }
    CS_HIGH();
}

static void cmd_flash_id(void)
{
    uint8_t id[3], sr1, sr2, sr3, sfdp[16];
    uint32_t i;

    spi_flash_setup(SPI_PSC_16);   /* 7.5 MHz */
    flash_cmd_read(0x9FU, id, 3U);
    flash_cmd_read(0x05U, &sr1, 1U);
    flash_cmd_read(0x35U, &sr2, 1U);
    flash_cmd_read(0x15U, &sr3, 1U);
    CS_LOW();
    spi_xfer(0x5AU);                /* SFDP: 3-byte address 0 + 1 dummy */
    spi_xfer(0U); spi_xfer(0U); spi_xfer(0U); spi_xfer(0xFFU);
    for(i = 0U; i < sizeof(sfdp); i++) {
        sfdp[i] = spi_xfer(0xFFU);
    }
    CS_HIGH();

    printf("JEDEC ID = %02X %02X %02X (expect C8 40 19 = GigaDevice GD25Q256)\n", id[0], id[1], id[2]);
    printf("SR1 = %02X (BUSY=%u WEL=%u BP=%u)  SR2 = %02X (ADS=%u, %s-byte mode)  SR3 = %02X\n", sr1,
           sr1 & 1U, (sr1 >> 1) & 1U, (sr1 >> 2) & 0x1FU, sr2, sr2 & 1U, (sr2 & 1U) ? "4" : "3", sr3);
    hexdump_line("SFDP", 0U, sfdp, sizeof(sfdp));
    printf("RESULT flash_id %s id=%02X%02X%02X sr1=%02X sr2=%02X sr3=%02X sfdp=%c%c%c%c\n",
           (id[0] == 0xC8U && id[1] == 0x40U && id[2] == 0x19U) ? "PASS" : "FAIL",
           id[0], id[1], id[2], sr1, sr2, sr3, sfdp[0], sfdp[1], sfdp[2], sfdp[3]);
}

static void cmd_flash_rd(void)
{
    uint32_t addr = arg_u32(2, 0U), len = arg_u32(3, 64U), n;
    uint8_t buf[32];

    spi_flash_setup(SPI_PSC_4);    /* 30 MHz */
    while(len) {
        n = (len > 32U) ? 32U : len;
        flash_read(addr, buf, n);
        hexdump_line("FL", addr, buf, n);
        addr += n;
        len -= n;
    }
    printf("RESULT flash_rd INFO done\n");
}

/* usage map: one character per 4 KB sector, '.' = all 0xFF, '#' = data; one line per 1 MB */
static void cmd_flash_map(void)
{
    static uint8_t buf[256];
    uint32_t mb, sec, off, i, used, total_used = 0U;
    char line[257];

    spi_flash_setup(SPI_PSC_4);
    for(mb = 0U; mb < 32U; mb++) {
        for(sec = 0U; sec < 256U; sec++) {
            uint32_t base = (mb << 20) | (sec << 12);
            used = 0U;
            for(off = 0U; off < 4096U && !used; off += sizeof(buf)) {
                flash_read(base + off, buf, sizeof(buf));
                for(i = 0U; i < sizeof(buf); i++) {
                    if(buf[i] != 0xFFU) {
                        used = 1U;
                        break;
                    }
                }
            }
            line[sec] = used ? '#' : '.';
            total_used += used;
        }
        line[256] = '\0';
        printf("MAP %02luMB %s\n", (unsigned long)mb, line);
    }
    printf("RESULT flash_map INFO used_sectors=%lu/8192\n", (unsigned long)total_used);
}

/* ------------------------------------------------------------------ EC25 modem */

static void gsm_pwrkey_pulse(uint32_t ms)
{
    gpio_bit_set(GSM_PWRKEY_PORT, GSM_PWRKEY_PIN);
    delay_ms(ms);
    gpio_bit_reset(GSM_PWRKEY_PORT, GSM_PWRKEY_PIN);
}

/* send an AT command, print every reply line, return 1 if OK, 0 on ERROR/timeout */
static int gsm_at(const char *cmd, uint32_t timeout_ms)
{
    char l[160];
    uint32_t n = 0U, start = millis();
    int c, got_ok = 0;

    uart_flush_rx(PORT_GSM);
    uart_puts(PORT_GSM, cmd);
    uart_puts(PORT_GSM, "\r");
    printf("GSM> %s\n", cmd);
    while((millis() - start) < timeout_ms) {
        c = uart_getc(PORT_GSM);
        if(c < 0) {
            continue;
        }
        if(c == '\n' || c == '\r') {
            if(n) {
                l[n] = '\0';
                printf("GSM< %s\n", l);
                if(!strcmp(l, "OK")) {
                    got_ok = 1;
                    break;
                }
                if(strstr(l, "ERROR")) {
                    break;
                }
                n = 0U;
            }
        } else if(n < sizeof(l) - 1U) {
            l[n++] = (char)c;
        }
    }
    if(!got_ok && (millis() - start) >= timeout_ms) {
        printf("GSM< (timeout %lu ms)\n", (unsigned long)timeout_ms);
    }
    return got_ok;
}

/* wait up to ms for unsolicited lines (RDY etc.), polling with AT every 2 s; returns 1 if AT answered */
static int gsm_wait_alive(uint32_t ms)
{
    uint32_t start = millis(), last_at = 0U;
    char l[96];
    uint32_t n = 0U;
    int c;

    while((millis() - start) < ms) {
        c = uart_getc(PORT_GSM);
        if(c >= 0) {
            if(c == '\n' || c == '\r') {
                if(n) {
                    l[n] = '\0';
                    printf("GSM< %s   (t=%lu ms)\n", l, (unsigned long)(millis() - start));
                    if(!strcmp(l, "OK")) {
                        return 1;
                    }
                    n = 0U;
                }
            } else if(n < sizeof(l) - 1U) {
                l[n++] = (char)c;
            }
        }
        if((millis() - last_at) >= 2000U) {
            last_at = millis();
            uart_puts(PORT_GSM, "AT\r");
        }
    }
    return 0;
}

static void gsm_power(int on)
{
    if(on) {
        gpio_bit_set(GSM_PWR_PORT, GSM_PWR_PIN);
    } else {
        gpio_bit_reset(GSM_PWR_PORT, GSM_PWR_PIN);
    }
}

static int gsm_session(void)
{
    int ok = 1;

    ok &= gsm_at("ATE0", 1000U);
    ok &= gsm_at("ATI", 1000U);
    gsm_at("AT+QGMR", 1000U);
    ok &= gsm_at("AT+CGSN", 1000U);
    gsm_at("AT+CIMI", 1000U);
    gsm_at("AT+CPIN?", 5000U);
    gsm_at("AT+QCCID", 1000U);
    gsm_at("AT+CSQ", 1000U);
    gsm_at("AT+CREG?", 1000U);
    gsm_at("AT+CEREG?", 1000U);
    gsm_at("AT+COPS?", 3000U);
    gsm_at("AT+IPR?", 1000U);
    gsm_at("AT+QGPS?", 1000U);
    gsm_at("AT+QCFG=\"band\"", 1000U);
    return ok;
}

/* full test: PE2 low, then PE2 high, modem left OFF at the end */
static void cmd_gsm_test(void)
{
    int alive_low, alive_high = 0, session = 0;

    uart_init(PORT_GSM, 115200U);

    printf("--- phase A: PE2 = 0 (modem supply disabled), PWRKEY pulse, listen 15 s ---\n");
    gsm_power(0);
    delay_ms(2000U);
    uart_flush_rx(PORT_GSM);
    gsm_pwrkey_pulse(600U);
    alive_low = gsm_wait_alive(15000U);
    printf("phase A: modem %s with PE2 = 0\n", alive_low ? "ANSWERED" : "silent");
    if(alive_low) {
        gsm_at("AT+QPOWD=1", 3000U);
        delay_ms(3000U);
    }

    printf("--- phase B: PE2 = 1 (modem supply enabled), PWRKEY 600 ms, wait for RDY / AT (30 s) ---\n");
    gsm_power(1);
    delay_ms(500U);
    uart_flush_rx(PORT_GSM);
    gsm_pwrkey_pulse(600U);
    alive_high = gsm_wait_alive(30000U);
    printf("phase B: modem %s with PE2 = 1\n", alive_high ? "ANSWERED" : "silent");
    if(alive_high) {
        session = gsm_session();
        gsm_at("AT+QPOWD=1", 3000U);
        delay_ms(3000U);
    }
    gsm_power(0);
    uart_deinit(PORT_GSM);

    printf("RESULT gsm %s pe2_low=%s pe2_high=%s session=%s\n",
           (!alive_low && alive_high && session) ? "PASS" : "FAIL",
           alive_low ? "answered" : "silent", alive_high ? "answered" : "silent", session ? "ok" : "fail");
}

static void cmd_gsm(void)
{
    const char *sub = (arg_count > 1) ? args[1] : "";

    if(!strcmp(sub, "test")) {
        cmd_gsm_test();
    } else if(!strcmp(sub, "on")) {
        uart_init(PORT_GSM, 115200U);
        gsm_power(1);
        delay_ms(500U);
        gsm_pwrkey_pulse(600U);
        printf("RESULT gsm_on %s\n", gsm_wait_alive(30000U) ? "PASS" : "FAIL");
    } else if(!strcmp(sub, "off")) {
        gsm_at("AT+QPOWD=1", 3000U);
        delay_ms(3000U);
        gsm_power(0);
        uart_deinit(PORT_GSM);
        printf("RESULT gsm_off INFO\n");
    } else if(!strcmp(sub, "pe2")) {
        gsm_power((int)arg_u32(2, 0U));
        printf("RESULT gsm_pe2 INFO PE2=%lu\n", (unsigned long)arg_u32(2, 0U));
    }
}

static void cmd_at(char *line)
{
    char *p = strchr(line, ' ');

    if(p) {
        printf("RESULT at %s\n", gsm_at(p + 1, 10000U) ? "PASS" : "FAIL");
    }
}

/* ------------------------------------------------------------------ YC1021 Bluetooth */

static void bt_reset(void)
{
    uart_init(PORT_BT, 115200U);
    gpio_bit_set(BT_EN_PORT, BT_EN_PIN);
    gpio_bit_reset(BT_RST_PORT, BT_RST_PIN);
    delay_ms(20U);
    uart_flush_rx(PORT_BT);
    gpio_bit_set(BT_RST_PORT, BT_RST_PIN);
}

static uint32_t bt_hci(const char *name, const uint8_t *pkt, uint32_t len)
{
    uint32_t n;

    uart_flush_rx(PORT_BT);
    hexdump_line("BT>", 0U, pkt, len);
    uart_write(PORT_BT, pkt, len);
    n = dump_rx(PORT_BT, "BT<", 500U, 50U);
    printf("  %s: %lu bytes back\n", name, (unsigned long)n);
    return n;
}

static void cmd_bt(void)
{
    static const uint8_t hci_reset[] = {0x01, 0x03, 0x0C, 0x00};
    static const uint8_t hci_ver[] = {0x01, 0x01, 0x10, 0x00};
    static const uint8_t hci_bdaddr[] = {0x01, 0x09, 0x10, 0x00};
    uint32_t boot, r1, r2, r3;

    bt_reset();
    printf("BT reset released, listening 1 s for boot output:\n");
    boot = dump_rx(PORT_BT, "BT<", 1000U, 0U);
    r1 = bt_hci("HCI_Reset", hci_reset, sizeof(hci_reset));
    r2 = bt_hci("HCI_Read_Local_Version", hci_ver, sizeof(hci_ver));
    r3 = bt_hci("HCI_Read_BD_ADDR", hci_bdaddr, sizeof(hci_bdaddr));
    printf("RESULT bt %s boot=%lu reset=%lu version=%lu bdaddr=%lu\n", (r1 || r2 || r3) ? "PASS" : "FAIL",
           (unsigned long)boot, (unsigned long)r1, (unsigned long)r2, (unsigned long)r3);
}

/* ------------------------------------------------------------------ dispatch */

static void cmd_help(void)
{
    printf("info                 clocks, reset reason, UID\n");
    printf("led [n on]           LED chase, or LED n (0..5) on/off\n");
    printf("adc                  12 V input and coin cell\n");
    printf("inputs               12 V / VBUS detect inputs\n");
    printf("rtc                  RTC / LXTAL status (read only)\n");
    printf("rs232 <1|2>          RS232 transceiver ack + loopback\n");
    printf("rs485 <1|2>          RS485 transceiver ack + cross test\n");
    printf("flash id             JEDEC ID, status, SFDP\n");
    printf("flash rd <addr> <n>  read-only hex dump\n");
    printf("flash map            used-sector map of all 32 MB\n");
    printf("gsm test|on|off      EC25 (test = PE2 low then high)\n");
    printf("gsm pe2 <0|1>        drive PE2 only\n");
    printf("at <cmd>             send AT command (modem must be on)\n");
    printf("bt                   YC1021 reset + HCI probe\n");
    printf("reboot               software reset\n");
}

void tests_dispatch(char *line)
{
    char copy[128];

    strncpy(copy, line, sizeof(copy) - 1U);
    copy[sizeof(copy) - 1U] = '\0';
    split_args(line);
    if(arg_count == 0) {
        return;
    }
    if(!strcmp(args[0], "help")) {
        cmd_help();
    } else if(!strcmp(args[0], "info")) {
        cmd_info();
    } else if(!strcmp(args[0], "ping")) {
        printf("pong\n");
    } else if(!strcmp(args[0], "led")) {
        cmd_led();
    } else if(!strcmp(args[0], "adc")) {
        cmd_adc();
    } else if(!strcmp(args[0], "inputs")) {
        cmd_inputs();
    } else if(!strcmp(args[0], "rtc")) {
        cmd_rtc();
    } else if(!strcmp(args[0], "rs232")) {
        cmd_rs232();
    } else if(!strcmp(args[0], "rs485")) {
        cmd_rs485();
    } else if(!strcmp(args[0], "rsdiag")) {
        cmd_rsdiag();
    } else if(!strcmp(args[0], "flash") && arg_count > 1) {
        if(!strcmp(args[1], "id")) {
            cmd_flash_id();
        } else if(!strcmp(args[1], "rd")) {
            cmd_flash_rd();
        } else if(!strcmp(args[1], "map")) {
            cmd_flash_map();
        }
    } else if(!strcmp(args[0], "gsm")) {
        cmd_gsm();
    } else if(!strcmp(args[0], "at")) {
        cmd_at(copy);
    } else if(!strcmp(args[0], "bt")) {
        cmd_bt();
    } else if(!strcmp(args[0], "reboot")) {
        NVIC_SystemReset();
    } else {
        printf("unknown command '%s' (try help)\n", args[0]);
    }
}
