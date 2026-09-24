/*
 * yc1021.c -- driver for the real BT/BLE chip on this board: a Yichip
 * YC1021 (QFN32) at board reference U5 (see yc1021.h for the chip-ID
 * history).
 *
 * PROTOCOL AND BRING-UP SEQUENCE, now taken from a proven, bench-verified
 * reference (`Old_PandaBox-feature-ble-lcr-emulator/Projects/03_PandaBox_
 * BLE_LCR/src/bt.c` + `bt_init_table.c`) instead of this project's own
 * earlier disassembly-reconstructed guess. That reference was verified
 * TODAY on this exact board (GD32F305VCT6, YC1021, same BT_RST=PD4/
 * BT_EN=PD5/UART3 pin map) with a real phone/PC BLE client completing
 * full command round-trips (BoxStatus, Start, GetData, Stop, etc.).
 * `bt_init_table.c` (copied verbatim into this project) is a 105-record
 * HCI patch/configuration upload extracted directly from Leo's real,
 * deployed X-Box V2.87 firmware binary -- not a guess. The earlier 7-step
 * guess this driver used skipped this table entirely, which is very
 * likely why it was never acknowledged: the chip needs its patch/config
 * uploaded before name/PIN/visibility commands mean anything to it.
 *
 * Frame formats (from that reference's bt.h, matching bt.c's actual
 * parsing/building code):
 *   TX to module:    [0x01][cmd][len][payload]
 *   Command ack:      02 06 02 <cmd> <status>          (status 0 = OK)
 *   HCI complete (init table only): 04 0E <len> 01 <op_lo> <op_hi> <status>
 *   Phone -> box (BLE write, handle 0x002D): 02 08 <len> 2D 00 <bytes>
 *   Phone -> box (SPP):                       02 07 <len> <bytes>
 *   Box -> phone (BLE notify, handle 0x002A): 01 09 <len> 2A 00 <bytes>, <=125B/chunk
 *   Box -> phone (SPP):                       01 05 <len> <bytes>
 *   Module status:    02 02 01 <status bits>  (bit0 BT-disc, bit1 BT-conn,
 *     bit2 BLE-adv, bit4 BT-LINK, bit5 BLE-LINK)
 *   Link closed:       02 05 00
 *
 * RX FIX, same root cause as ec25.c's: this MCU's USART has only a
 * ONE-BYTE hardware receive register and no FIFO. Polling once per
 * FreeRTOS tick (5ms) can lose all but the last byte of a fast HCI
 * event burst arriving between two poll calls. Fixed the same way:
 * interrupt-driven RX into a ring buffer.
 *
 * REAL HARDWARE RISK: this is the first time THIS project has sent the
 * 105-record init table + name/PIN/MAC/visibility sequence to THIS
 * physical board (it is proven on a different unit of the same design,
 * not yet observed here). Bring-up runs once, blocking, inside
 * yc1021_init() -- if it doesn't complete, the chip is left however the
 * partial sequence leaves it; see the port plan's Step 1b safety notes
 * before re-attempting on real hardware without someone watching.
 */
#include "yc1021.h"
#include "uart.h"
#include "gpio.h"
#include "log.h"
#include "regs.h"
#include "board_config.h"
#include "cmd.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

extern const uint16_t bt_init_table_count;
extern const uint16_t bt_init_table_size;
extern const uint8_t  bt_init_table[];

static const uart_port_t s_port = {
    .base    = YC1021_UART_BASE,
    .tx_port = YC1021_GPIO_PORT, .tx_pin = YC1021_TX_PIN,
    .rx_port = YC1021_GPIO_PORT, .rx_pin = YC1021_RX_PIN,
};

/* ------------------------------------------------------------------ *
 * Interrupt-driven RX ring buffer -- see the header comment above and
 * ec25.c's identical fix for why this exists.
 * ------------------------------------------------------------------ */
#define YC1021_RX_RING_SIZE 256u
static volatile uint8_t  s_rx_ring[YC1021_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint32_t s_rx_overruns;

void UART3_IRQHandler(void)
{
    if ((YC1021_UART_BASE->STAT & (USART_STAT_RBNE | USART_STAT_ORERR)) != 0) {
        uint8_t b = (uint8_t)YC1021_UART_BASE->DATA;
        uint32_t next = (s_rx_head + 1u) % YC1021_RX_RING_SIZE;
        if (next != s_rx_tail) {
            s_rx_ring[s_rx_head] = b;
            s_rx_head = next;
        } else {
            s_rx_overruns++;
        }
    }
}

static int yc1021_rx_ready(void)
{
    return s_rx_head != s_rx_tail;
}

static uint8_t yc1021_rx_getc(void)
{
    uint8_t b = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1u) % YC1021_RX_RING_SIZE;
    return b;
}

/* ------------------------------------------------------------------ *
 * Raw RX tap -- logs every byte actually received, independent of
 * whatever framing we think we understand.
 * ------------------------------------------------------------------ */
static uint8_t  raw_buf[16];
static uint32_t raw_len;

static void raw_rx_flush(void)
{
    if (raw_len > 0u) {
        if (cmd_debug_enabled()) {
            log_hex("[BLE DBG] RAW RX: ", raw_buf, raw_len);
        }
        raw_len = 0u;
    }
}

static void raw_rx_tap(uint8_t b)
{
    raw_buf[raw_len++] = b;
    if (raw_len >= sizeof(raw_buf)) {
        raw_rx_flush();
    }
}

/* ------------------------------------------------------------------ *
 * Blocking waits used only during bring-up (yc1021_init()'s own task
 * context -- same accepted pattern as ec25.c's GPS functions).
 * ------------------------------------------------------------------ */

/* Yichip command ack: 02 06 02 <cmd> <status> (status 0 = OK). */
static int wait_cmd_ack(uint8_t cmd, uint32_t timeout_ms)
{
    uint8_t ev[8];
    uint32_t ev_n = 0u;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        while (yc1021_rx_ready()) {
            uint8_t b = yc1021_rx_getc();
            raw_rx_tap(b);
            if (ev_n == 0u && b != 0x02u) {
                continue;
            }
            if (ev_n < sizeof(ev)) {
                ev[ev_n] = b;
            }
            ev_n++;
            if (ev_n >= 3u && ev_n == 3u + ev[2]) {
                if (ev[1] == 0x06u && ev[2] >= 2u && ev[3] == cmd) {
                    raw_rx_flush();
                    return ev[4] == 0u;
                }
                ev_n = 0u;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    raw_rx_flush();
    return 0;
}

/* Standard HCI command-complete, used only for the init table upload:
 * 04 0E <len> 01 <op_lo> <op_hi> <status>. */
static int wait_hci_complete(uint16_t op, uint32_t timeout_ms)
{
    uint8_t e[16];
    uint32_t n = 0u;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        while (yc1021_rx_ready()) {
            uint8_t b = yc1021_rx_getc();
            raw_rx_tap(b);
            if (n == 0u && b != 0x04u) {
                continue;
            }
            if (n < sizeof(e)) {
                e[n] = b;
            }
            n++;
            if (n >= 3u && n == 3u + e[2]) {
                if (e[1] == 0x0Eu && n >= 7u && (uint16_t)(e[4] | (e[5] << 8)) == op) {
                    raw_rx_flush();
                    return e[6] == 0u;
                }
                n = 0u;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    raw_rx_flush();
    return 0;
}

static int yc1021_cmd(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t pkt[40];
    int attempt;

    pkt[0] = 0x01u;
    pkt[1] = cmd;
    pkt[2] = len;
    memcpy(&pkt[3], data, len);
    for (attempt = 0; attempt < 4; attempt++) {
        log_line("YC1021 TX cmd 0x");
        log_hex("", &cmd, 1);
        uart_write(&s_port, pkt, 3u + len);
        if (wait_cmd_ack(cmd, 1000u)) {
            return 1;
        }
    }
    return 0;
}

static int yc1021_cmd_once(uint8_t cmd, const uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    uint8_t pkt[8];
    pkt[0] = 0x01u;
    pkt[1] = cmd;
    pkt[2] = len;
    memcpy(&pkt[3], data, len);
    uart_write(&s_port, pkt, 3u + len);
    return wait_cmd_ack(cmd, timeout_ms);
}

static int yc1021_set_name(const char *name)
{
    uint8_t le[24];
    size_t n = strlen(name);
    int ok = 1;

    if (n > 16u) {
        n = 16u;
    }
    ok &= yc1021_cmd(0x03u, (const uint8_t *)name, (uint8_t)n);
    if (n > 13u) {
        n = 13u; /* BLE name = name + "BLE", max 16 -- matches SKILL.md's {name}ble convention */
    }
    memcpy(le, name, n);
    memcpy(&le[n], "BLE", 3u);
    ok &= yc1021_cmd(0x04u, le, (uint8_t)(n + 3u));
    return ok;
}

static int upload_init_table(void)
{
    uint32_t off = 0u, idx = 0u, ok = 0u;

    while (off < bt_init_table_size && idx < bt_init_table_count) {
        uint32_t n = bt_init_table[off];
        uint16_t op = (uint16_t)(bt_init_table[off + 2u] | (bt_init_table[off + 3u] << 8));
        uart_write(&s_port, &bt_init_table[off + 1u], n);
        if (wait_hci_complete(op, 500u)) {
            ok++;
        }
        off += 1u + n;
        idx++;
    }
    log_line("YC1021: init table ");
    log_uint(ok);
    log_line("/");
    log_uint(bt_init_table_count);
    log_line(" records acknowledged\r\n");
    return ok == bt_init_table_count;
}

/* ------------------------------------------------------------------ *
 * Steady-state: event parser, line assembler, reply sender, and the
 * self-heal re-advertise logic (all non-blocking except the chunk-ack
 * wait inside yc1021_send_line, which is only entered synchronously
 * while replying to a just-received command).
 * ------------------------------------------------------------------ */
typedef enum { YC_LINK_BLE = 0, YC_LINK_SPP = 1 } yc_link_t;

#define ASM_LINE_MAX 160u
static char     asm_buf[2][ASM_LINE_MAX];
static uint32_t asm_n[2];

static uint8_t   st_last = 0xFFu;
static int       online;          /* 1 once a BT or BLE link is connected */
static int       bt_up;           /* 1 once bring-up completed successfully */
static int       readv_pending;
static TickType_t readv_at;
static uint32_t  readv_misses;

#define BT_CHUNK_MAX      125u
#define BLE_NOTIFY_HANDLE 0x002Au

static void yc1021_send_line(yc_link_t link, const char *line)
{
    static uint8_t pkt[4u + 2u + BT_CHUNK_MAX];
    char buf[ASM_LINE_MAX + 4u];
    uint32_t len, off = 0u, chunk, hdr;
    uint8_t cmd = (link == YC_LINK_BLE) ? 0x09u : 0x05u;

    len = (uint32_t)snprintf(buf, sizeof(buf), "%s\r\n", line);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;
    }
    while (off < len) {
        chunk = len - off;
        if (chunk > BT_CHUNK_MAX) {
            chunk = BT_CHUNK_MAX;
        }
        pkt[0] = 0x01u;
        pkt[1] = cmd;
        if (link == YC_LINK_BLE) {
            pkt[2] = (uint8_t)(chunk + 2u);
            pkt[3] = (uint8_t)(BLE_NOTIFY_HANDLE & 0xFFu);
            pkt[4] = (uint8_t)(BLE_NOTIFY_HANDLE >> 8);
            hdr = 5u;
        } else {
            pkt[2] = (uint8_t)chunk;
            hdr = 3u;
        }
        memcpy(&pkt[hdr], &buf[off], chunk);
        uart_write(&s_port, pkt, hdr + chunk);
        (void)wait_cmd_ack(cmd, 200u); /* wait for send-done before the next chunk */
        off += chunk;
    }
}

static void yc1021_assemble(yc_link_t link, const uint8_t *d, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        char c = (char)d[i];
        if (c == '\n') {
            while (asm_n[link] > 0u &&
                   (asm_buf[link][asm_n[link] - 1u] == '\r' || asm_buf[link][asm_n[link] - 1u] == ' ')) {
                asm_n[link]--;
            }
            asm_buf[link][asm_n[link]] = '\0';
            if (asm_n[link] > 0u) {
                char reply[200];
                uint32_t reply_len = cmd_process_line(asm_buf[link], asm_n[link], reply, sizeof(reply));

                /* Mirror every received BLE/SPP command to both RTT and debug UART */
                log_line(link == YC_LINK_BLE ? "[BLE RX] " : "[SPP RX] ");
                log_line(asm_buf[link]);
                log_line("\r\n");

                if (reply_len > 0u) {
                    while (reply_len > 0u && (reply[reply_len - 1u] == '\n' || reply[reply_len - 1u] == '\r')) {
                        reply_len--;
                    }
                    reply[reply_len] = '\0';

                    /* Mirror the reply going back to BLE/SPP to both RTT and debug UART */
                    log_line(link == YC_LINK_BLE ? "[BLE TX] " : "[SPP TX] ");
                    log_line(reply);
                    log_line("\r\n");

                    yc1021_send_line(link, reply);
                }
            }
            asm_n[link] = 0u;
        } else if (asm_n[link] < ASM_LINE_MAX - 1u) {
            asm_buf[link][asm_n[link]++] = c;
        }
    }
}

static void handle_event(const uint8_t *e, uint32_t n)
{
    (void)n;
    switch (e[1]) {
    case 0x08u: /* BLE data: 02 08 len <handle_lo> <handle_hi> data */
        if (e[2] >= 2u) {
            yc1021_assemble(YC_LINK_BLE, &e[5], (uint32_t)e[2] - 2u);
        }
        break;
    case 0x07u: /* SPP data */
        yc1021_assemble(YC_LINK_SPP, &e[3], e[2]);
        break;
    case 0x02u: /* module status */
        if (e[2] >= 1u) {
            st_last = e[3];
            log_line("YC1021: status 0x");
            log_hex("", &st_last, 1);
            if ((st_last & 0x30u) == 0u && (st_last & 0x07u) != 0x07u) {
                readv_pending = 1;
                readv_at = xTaskGetTickCount() + pdMS_TO_TICKS(150);
            } else {
                readv_pending = 0;
            }
            online = (st_last & 0x30u) != 0u;
            if (online) {
                asm_n[0] = asm_n[1] = 0u;
            }
        }
        break;
    case 0x05u: /* link closed */
        log_line("YC1021: link closed\r\n");
        online = 0;
        asm_n[0] = asm_n[1] = 0u;
        break;
    default:
        break;
    }
}

static uint8_t  s_ev[140];
static uint32_t s_ev_n;

void yc1021_init(void)
{
    static const uint8_t pair_mode[] = { 0x00u };
    static const uint8_t pin[]       = { '1', '2', '3', '4' };
    static const uint8_t visible[]   = { 0x07u };
    uint32_t uid = *(volatile uint32_t *)0x1FFFF7E8u;
    uint8_t  mac[6];
    uint32_t v;
    int      ok;

    /* Real bug fix (found earlier this session, still correct): PD4 has
     * no pull resistor, so an undriven reset line sits at an undefined
     * level. Sequence below matches the proven reference: enable
     * BT_3.3V, assert reset, settle, release, settle again, flush. */
    gpio_set_output_high(YC1021_ENABLE_PORT, YC1021_ENABLE_PIN);
    gpio_set_output_low(YC1021_RESET_PORT, YC1021_RESET_PIN);

    uart_init(&s_port, YC1021_UART_BAUD, BOARD_PCLK1_HZ);
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_overruns = 0;
    YC1021_UART_BASE->CTL0 |= USART_CTL0_RBNEIE;
    nvic_enable_irq(UART3_IRQN);

    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_output_high(YC1021_RESET_PORT, YC1021_RESET_PIN);
    vTaskDelay(pdMS_TO_TICKS(100));
    while (yc1021_rx_ready()) {
        (void)yc1021_rx_getc(); /* flush any stale/boot bytes before the real sequence */
    }

    log_line("\r\n--- YC1021: uploading 105-record init table (real, from Leo's firmware) ---\r\n");
    ok = upload_init_table();
    vTaskDelay(pdMS_TO_TICKS(300));
    s_ev_n = 0u;

    ok &= yc1021_set_name("PandaBrain");
    ok &= yc1021_cmd(0x0Cu, pair_mode, 1u);
    ok &= yc1021_cmd(0x0Du, pin, 4u);

    v = uid + 4u;
    mac[0] = (uint8_t)v; mac[1] = (uint8_t)(v >> 8); mac[2] = (uint8_t)(v >> 16); mac[3] = (uint8_t)(v >> 24);
    mac[4] = 0x11u; mac[5] = 0x25u;
    ok &= yc1021_cmd(0x00u, mac, 6u); /* BT address */

    v = uid + 5u;
    mac[0] = (uint8_t)v; mac[1] = (uint8_t)(v >> 8); mac[2] = (uint8_t)(v >> 16); mac[3] = (uint8_t)(v >> 24);
    mac[4] = 0x11u; mac[5] = 0xE7u;
    ok &= yc1021_cmd(0x01u, mac, 6u); /* BLE address */

    ok &= yc1021_cmd(0x02u, visible, 1u); /* BT discoverable + connectable + BLE advertising */

    bt_up = ok;
    log_line(ok ? "YC1021: bring-up complete -- advertising as \"PandaBrain\" / \"PandaBrainBLE\"\r\n"
                : "YC1021: bring-up INCOMPLETE -- one or more steps were not acknowledged\r\n");

    online = 0;
    asm_n[0] = asm_n[1] = 0u;
    readv_pending = 0;
    readv_misses = 0u;
}

void yc1021_poll(void)
{
    while (yc1021_rx_ready()) {
        uint8_t b = yc1021_rx_getc();
        raw_rx_tap(b);
        if (s_ev_n == 0u && b != 0x02u) {
            continue; /* resync on packet type -- steady state only cares about 0x02 events */
        }
        if (s_ev_n < sizeof(s_ev)) {
            s_ev[s_ev_n] = b;
        }
        s_ev_n++;
        if (s_ev_n >= 3u && s_ev_n == 3u + s_ev[2]) {
            handle_event(s_ev, s_ev_n);
            s_ev_n = 0u;
        } else if (s_ev_n >= sizeof(s_ev)) {
            s_ev_n = 0u;
        }
    }
    raw_rx_flush();

    if (!bt_up) {
        return;
    }

    /* Self-heal, matching the proven reference's bt_service(): re-apply
     * visibility when the module reports it stopped advertising and
     * isn't connected. No periodic poke otherwise -- the reference found
     * that visibility commands sent while a central is mid-connect can
     * break the connection. */
    if (readv_pending && (int32_t)(xTaskGetTickCount() - readv_at) >= 0) {
        static const uint8_t visible[] = { 0x07u };
        readv_pending = 0;
        if (yc1021_cmd_once(0x02u, visible, 1u, 500u)) {
            readv_misses = 0u;
        } else {
            readv_misses++;
            readv_pending = 1;
            readv_at = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
        }
    }
    if (readv_misses >= 2u) {
        readv_misses = 0u;
        readv_pending = 0u;
        log_line("YC1021: module silent -- re-running full bring-up\r\n");
        yc1021_init();
    }
}

int yc1021_is_synced(void)
{
    return online;
}
