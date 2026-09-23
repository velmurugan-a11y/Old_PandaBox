/*!
    \file    bt.c
    \brief   Yichip YC1021 Bluetooth driver (see bt.h for the frame formats)
*/

#include <stdio.h>
#include <string.h>
#include "bt.h"
#include "board.h"
#include "uart.h"
#include "app.h"

extern const uint16_t bt_init_table_count;
extern const uint16_t bt_init_table_size;
extern const uint8_t bt_init_table[];

#define BLE_NOTIFY_HANDLE   0x002AU
#define BT_CHUNK_MAX        125U
#define LINE_MAX            160U
#define LINE_QUEUE          4U

uint8_t bt_trace = 0U;

static uint8_t up;
static uint32_t rx_bytes, tx_frames;

/* module status (event 02 02 <status>): bit0 BT discoverable, bit1 BT connectable, bit2 BLE advertising,
   bit4 BT connected, bit5 BLE connected. The module drops to 00 after a link ends (seen on the bench:
   "02 02 00" and then the box no longer showed in any scan), so visibility is re-applied then. */
#define ST_ADV_MASK     0x07U
#define ST_CONN_MASK    0x30U
static uint8_t st_last = 0xFFU, readv_pending;
static uint32_t readv_at, readv_count, last_rx_ms, restarts;

/* event parser */
static uint8_t ev[140];
static uint32_t ev_n;
static volatile uint8_t last_ack_cmd, last_ack_status, ack_seen;

/* line assembler (one per link) + completed-line queue */
static char asm_buf[2][LINE_MAX];
static uint32_t asm_n[2];
static char q_line[LINE_QUEUE][LINE_MAX];
static bt_link_t q_link[LINE_QUEUE];
static uint32_t q_head, q_count;

static void trace_frame(const char *dir, const uint8_t *b, uint32_t n)
{
    uint32_t i;

    if(!bt_trace) {
        return;
    }
    printf("BT%s", dir);
    for(i = 0U; i < n; i++) {
        printf(" %02X", b[i]);
    }
    printf("\n");
}

static void assemble(bt_link_t link, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    char c;

    rx_bytes += n;
    for(i = 0U; i < n; i++) {
        c = (char)d[i];
        if(c == '\n') {
            while(asm_n[link] && (asm_buf[link][asm_n[link] - 1U] == '\r' || asm_buf[link][asm_n[link] - 1U] == ' ')) {
                asm_n[link]--;
            }
            asm_buf[link][asm_n[link]] = '\0';
            if(asm_n[link] && q_count < LINE_QUEUE) {
                uint32_t slot = (q_head + q_count) % LINE_QUEUE;
                memcpy(q_line[slot], asm_buf[link], asm_n[link] + 1U);
                q_link[slot] = link;
                q_count++;
            }
            asm_n[link] = 0U;
        } else if(asm_n[link] < LINE_MAX - 1U) {
            asm_buf[link][asm_n[link]++] = c;
        }
    }
}

static void handle_event(const uint8_t *e, uint32_t n)
{
    trace_frame("<", e, n);
    switch(e[1]) {
    case 0x06U:                         /* command acknowledge: 02 06 02 <cmd> <status> */
        if(e[2] >= 2U) {
            last_ack_cmd = e[3];
            last_ack_status = e[4];
            ack_seen = 1U;
        }
        break;
    case 0x08U:                         /* BLE data: 02 08 len <handle lo> <handle hi> data */
        if(e[2] >= 2U) {
            assemble(BT_LINK_BLE, &e[5], (uint32_t)e[2] - 2U);
        }
        break;
    case 0x07U:                         /* SPP data */
        assemble(BT_LINK_SPP, &e[3], e[2]);
        break;
    case 0x02U:                         /* module status */
        if(e[2] >= 1U) {
            st_last = e[3];
            printf("[%lu] BT status %02X:%s%s%s%s%s\n", (unsigned long)millis(), st_last,
                   (st_last & 0x01U) ? " BT-disc" : "", (st_last & 0x02U) ? " BT-conn" : "",
                   (st_last & 0x04U) ? " BLE-adv" : "", (st_last & 0x10U) ? " BT-LINK" : "",
                   (st_last & 0x20U) ? " BLE-LINK" : "");
            if((st_last & ST_CONN_MASK) == 0U && (st_last & ST_ADV_MASK) != ST_ADV_MASK) {
                readv_pending = 1U;     /* not connected and not fully visible: advertise again */
                readv_at = millis() + 150U;
            } else {
                readv_pending = 0U;
            }
            if(st_last & ST_CONN_MASK) {
                asm_n[0] = asm_n[1] = 0U;   /* fresh line buffers for the new link */
            }
        }
        break;
    case 0x05U:                         /* 02 05 00: link closed (seen when the central disconnects;
                                           the module resumes advertising by itself) */
        printf("[%lu] BT: link closed\n", (unsigned long)millis());
        asm_n[0] = asm_n[1] = 0U;
        break;
    default:                            /* 02 03 rssi, 02 09 ready, 02 0F busy... */
        app_log_bt_event(e, n);
        break;
    }
}

void bt_poll(void)
{
    int c;

    while((c = uart_getc(PORT_BT)) >= 0) {
        if(ev_n == 0U && c != 0x02 && c != 0x04) {
            continue;                   /* resync on packet type */
        }
        ev[ev_n++] = (uint8_t)c;
        last_rx_ms = millis();
        if(ev_n >= 3U && ev_n == 3U + ev[2]) {
            if(ev[0] == 0x02U) {
                handle_event(ev, ev_n);
            } else {
                trace_frame("<", ev, ev_n);   /* HCI event during upload */
                ack_seen = 1U;
                last_ack_cmd = 0xFFU;
            }
            ev_n = 0U;
        } else if(ev_n >= sizeof(ev)) {
            ev_n = 0U;
        }
    }
}

int bt_get_line(char *out, uint32_t max, bt_link_t *link)
{
    if(q_count == 0U) {
        return 0;
    }
    strncpy(out, q_line[q_head], max - 1U);
    out[max - 1U] = '\0';
    *link = q_link[q_head];
    q_head = (q_head + 1U) % LINE_QUEUE;
    q_count--;
    return 1;
}

/* HCI event wait during the table upload: 04 0E len 01 <op lo> <op hi> <status> */
static int wait_hci_complete(uint16_t op, uint32_t timeout_ms)
{
    uint8_t e[16];
    uint32_t n = 0U, start = millis();
    int c;

    while((millis() - start) < timeout_ms) {
        c = uart_getc(PORT_BT);
        if(c < 0) {
            continue;
        }
        if(n == 0U && c != 0x04) {
            continue;
        }
        if(n < sizeof(e)) {
            e[n] = (uint8_t)c;
        }
        n++;
        if(n >= 3U && n == 3U + e[2]) {
            if(e[1] == 0x0EU && n >= 7U && (e[4] | (e[5] << 8)) == op) {
                return e[6] == 0U;
            }
            n = 0U;
        }
    }
    return 0;
}

/* Yichip command with acknowledge 02 06 02 <cmd> 00; retries while the module is busy */
static int yc_cmd(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t pkt[40];
    uint32_t t, attempt;

    pkt[0] = 0x01U;
    pkt[1] = cmd;
    pkt[2] = len;
    memcpy(&pkt[3], data, len);
    for(attempt = 0U; attempt < 4U; attempt++) {
        ack_seen = 0U;
        trace_frame(">", pkt, 3U + len);
        uart_write(PORT_BT, pkt, 3U + len);
        t = millis();
        while((millis() - t) < 1000U) {
            bt_poll();
            if(ack_seen && last_ack_cmd == cmd) {
                return last_ack_status == 0U;
            }
        }
    }
    return 0;
}

/* single attempt, used by bt_service() so the main loop is not blocked for long */
static int yc_cmd_once(uint8_t cmd, const uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    uint8_t pkt[40];
    uint32_t t;

    pkt[0] = 0x01U;
    pkt[1] = cmd;
    pkt[2] = len;
    memcpy(&pkt[3], data, len);
    ack_seen = 0U;
    trace_frame(">", pkt, 3U + len);
    uart_write(PORT_BT, pkt, 3U + len);
    t = millis();
    while((millis() - t) < timeout_ms) {
        bt_poll();
        if(ack_seen && last_ack_cmd == cmd) {
            return last_ack_status == 0U;
        }
    }
    return 0;
}

int bt_set_name(const char *name)
{
    uint8_t le[24];
    size_t n = strlen(name);
    int ok = 1;

    if(n > 16U) {
        n = 16U;
    }
    ok &= yc_cmd(0x03U, (const uint8_t *)name, (uint8_t)n);
    if(n > 13U) {
        n = 13U;                        /* Leo: BLE name = name + "BLE", max 16 */
    }
    memcpy(le, name, n);
    memcpy(&le[n], "BLE", 3U);
    ok &= yc_cmd(0x04U, le, (uint8_t)(n + 3U));
    return ok;
}

int bt_start(const char *name)
{
    static const uint8_t pair_mode[] = {0x00};
    static const uint8_t pin[] = {'1', '2', '3', '4'};
    static const uint8_t visible[] = {0x07};    /* BT discoverable + connectable + BLE advertising */
    uint32_t uid = *(volatile uint32_t *)0x1FFFF7E8U;
    uint32_t off = 0U, idx = 0U, ok_rec = 0U, i, v;
    uint8_t mac[6];
    uint16_t op;
    int ok;

    up = 0U;
    uart_init(PORT_BT, 115200U);
    gpio_bit_set(BT_EN_PORT, BT_EN_PIN);
    gpio_bit_reset(BT_RST_PORT, BT_RST_PIN);
    delay_ms(20U);
    gpio_bit_set(BT_RST_PORT, BT_RST_PIN);
    delay_ms(100U);
    uart_flush_rx(PORT_BT);

    /* 1. patch + configuration upload */
    while(off < bt_init_table_size && idx < bt_init_table_count) {
        uint32_t n = bt_init_table[off];
        op = (uint16_t)(bt_init_table[off + 2U] | (bt_init_table[off + 3U] << 8));
        uart_write(PORT_BT, &bt_init_table[off + 1U], n);
        if(wait_hci_complete(op, 500U)) {
            ok_rec++;
        }
        off += 1U + n;
        idx++;
    }
    printf("BT: init table %lu/%u records acknowledged\n", (unsigned long)ok_rec, bt_init_table_count);
    delay_ms(300U);
    ev_n = 0U;

    /* 2. name, pairing, PIN, addresses (UID word + 4 / + 5, then 11 25), visibility */
    ok = (ok_rec == bt_init_table_count);
    ok &= bt_set_name(name);
    ok &= yc_cmd(0x0CU, pair_mode, 1U);
    ok &= yc_cmd(0x0DU, pin, 4U);
    for(i = 0U; i < 2U; i++) {
        v = uid + 4U + i;
        mac[0] = (uint8_t)v; mac[1] = (uint8_t)(v >> 8); mac[2] = (uint8_t)(v >> 16); mac[3] = (uint8_t)(v >> 24);
        mac[4] = 0x11U; mac[5] = i ? 0xE7U : 0x25U;   /* BLE address MSB: E5, E6 already used on the bench PC (see README, reconnect issue) */
        ok &= yc_cmd(i ? 0x01U : 0x00U, mac, 6U);
    }
    ok &= yc_cmd(0x02U, visible, 1U);
    up = (uint8_t)ok;
    printf("BT: %s, advertising as \"%s\" / \"%.13sBLE\"\n", ok ? "ready" : "config incomplete", name, name);
    return ok;
}

void bt_send_line(bt_link_t link, const char *line)
{
    static uint8_t pkt[4U + 2U + BT_CHUNK_MAX];
    char buf[LINE_MAX + 4U];
    uint32_t len, off = 0U, chunk, hdr, t;
    uint8_t cmd = (link == BT_LINK_BLE) ? 0x09U : 0x05U;

    len = (uint32_t)snprintf(buf, sizeof(buf), "%s\r\n", line);
    if(len >= sizeof(buf)) {
        len = sizeof(buf) - 1U;
    }
    while(off < len) {
        chunk = len - off;
        if(chunk > BT_CHUNK_MAX) {
            chunk = BT_CHUNK_MAX;
        }
        pkt[0] = 0x01U;
        pkt[1] = cmd;
        if(link == BT_LINK_BLE) {
            pkt[2] = (uint8_t)(chunk + 2U);
            pkt[3] = (uint8_t)(BLE_NOTIFY_HANDLE & 0xFFU);
            pkt[4] = (uint8_t)(BLE_NOTIFY_HANDLE >> 8);
            hdr = 5U;
        } else {
            pkt[2] = (uint8_t)chunk;
            hdr = 3U;
        }
        memcpy(&pkt[hdr], &buf[off], chunk);
        ack_seen = 0U;
        trace_frame(">", pkt, hdr + chunk);
        uart_write(PORT_BT, pkt, hdr + chunk);
        tx_frames++;
        /* wait for 02 06 02 09 00 (send done) before the next chunk */
        t = millis();
        while((millis() - t) < 200U) {
            bt_poll();
            if(ack_seen && last_ack_cmd == cmd) {
                break;
            }
        }
        off += chunk;
    }
}

/* HCI_Reset 01 03 0C 00 -> 04 0E 04 01 03 0C 00: only answered by the module ROM, i.e. after the
   module rebooted itself and lost the patch/configuration */
static int hci_probe(void)
{
    static const uint8_t hci_reset[] = {0x01, 0x03, 0x0C, 0x00};

    uart_flush_rx(PORT_BT);
    uart_write(PORT_BT, hci_reset, sizeof(hci_reset));
    return wait_hci_complete(0x0C03U, 300U);
}

/* call from the main loop: re-applies visibility when the module reports (02 02) that it stopped
   advertising. No periodic command is sent: the module reports no event while a central sets up a
   link, and a visibility command in that window broke the connection on the bench. When the
   re-advertise command is not acknowledged twice, the module is probed with HCI_Reset and
   brought up again from scratch. */
void bt_service(void)
{
    static const uint8_t visible[] = {0x07};
    static uint32_t misses;

    if(readv_pending && (int32_t)(millis() - readv_at) >= 0) {
        readv_pending = 0U;
        readv_count++;
        printf("[%lu] BT: re-enable advertising (%lu)\n", (unsigned long)millis(), (unsigned long)readv_count);
        if(yc_cmd_once(0x02U, visible, 1U, 500U)) {
            misses = 0U;
        } else {
            misses++;
            readv_pending = 1U;
            readv_at = millis() + 1000U;
        }
    }
    if(misses >= 2U) {
        misses = 0U;
        readv_pending = 0U;
        restarts++;
        printf("[%lu] BT: module silent, HCI probe %s -> restart #%lu\n", (unsigned long)millis(),
               hci_probe() ? "answered (module had rebooted itself)" : "no answer (module hung)",
               (unsigned long)restarts);
        bt_start(app_bt_name());
        st_last = 0xFFU;
    }
}

uint32_t bt_restarts(void)
{
    return restarts;
}

uint8_t bt_status(void)
{
    return st_last;
}

uint8_t bt_is_up(void)
{
    return up;
}

uint32_t bt_rx_bytes(void)
{
    return rx_bytes;
}

uint32_t bt_tx_frames(void)
{
    return tx_frames;
}
