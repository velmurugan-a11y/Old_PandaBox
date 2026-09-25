/*!
    \file    lcr_host.c
    \brief   PandaBox side of the LCR link

    Every request is a real LCP frame (lcp_build) and every answer is parsed with lcp_parse, exactly
    as on the RS232/RS485 wire. The transport below hands the frame to the simulated meter on that
    port; replacing lcr_transport() with a UART exchange connects real meters.
*/

#include <stdio.h>
#include <string.h>
#include "lcr_host.h"
#include "lcp.h"
#include "meter.h"
#include "app.h"
#include "app_compat.h"

lcr_port_t lcr_port[LCR_PORTS];
uint8_t lcr_trace = 0U;

/* the two simulated meters: meter 1 (node 1) on port 1, meter 2 (node 2) on port 2 */
static meter_t sim_meter[LCR_PORTS];
static uint8_t msg_toggle[LCR_PORTS];

/* fields polled each cycle, in Leo's order */
static const uint8_t poll_fields[6] = {2U, 4U, 17U, 18U, 100U, 101U};

meter_t *lcr_sim_meter(int port)
{
    return &sim_meter[port];
}

static void trace(const char *dir, int port, const uint8_t *b, uint32_t n)
{
    uint32_t i;

    if(!lcr_trace) {
        return;
    }
    printf("LCP%d%s", port + 1, dir);
    for(i = 0U; i < n; i++) {
        printf(" %02X", b[i]);
    }
    printf("\n");
}

/* Real LCP over the port's RS232 UART (port 0 = USART1/J1, port 1 = USART2/J2).
 * This is the "circle": the PandaBox talks to the external LCR meter / PC simulator on the wire,
 * exactly like Leo's firmware. Set LCR_USE_SIM to 1 to fall back to the internal simulator. */
#ifndef LCR_USE_SIM
#define LCR_USE_SIM 0
#endif

extern uint32_t millis(void);
extern void     lcr_hal_uart_send(int port, const uint8_t *d, uint32_t n);
extern int      lcr_hal_uart_read(int port);   /* -1 when empty */
extern void     lcr_hal_uart_flush(int port);

static uint32_t lcr_transport(int port, const uint8_t *req, uint32_t n, uint8_t *rsp)
{
    uint32_t r = 0U;
    uint32_t t0, last;
    int c;

    trace(" >", port, req, n);
#if LCR_USE_SIM
    r = meter_lcp(&sim_meter[port], req, n, rsp);
#else
    lcr_hal_uart_flush(port);
    lcr_hal_uart_send(port, req, n);
    /* wait up to 120 ms for the first reply byte, then read until a ~15 ms idle gap (Leo: 50 ms pack,
       120 ms field timeout). A meter frame is <= ~16 bytes. */
    t0 = millis();
    while((millis() - t0) < 120U) {
        c = lcr_hal_uart_read(port);
        if(c >= 0) {
            break;
        }
    }
    if(c < 0) {
        return 0U;   /* no answer -> meter offline (matches Leo's timeout behaviour) */
    }
    rsp[r++] = (uint8_t)c;
    last = millis();
    while(r < LCP_MAX_FRAME && (millis() - last) < 15U) {
        c = lcr_hal_uart_read(port);
        if(c >= 0) {
            rsp[r++] = (uint8_t)c;
            last = millis();
        }
    }
#endif
    if(r) {
        trace(" <", port, rsp, r);
    }
    return r;
}

/* one request/response; returns response data length or -1 */
static int lcr_xfer(int port, uint8_t to, const uint8_t *data, uint8_t len, lcp_frame_t *rsp)
{
    static uint8_t req[LCP_MAX_FRAME], raw[LCP_MAX_FRAME];
    uint32_t n, r;
    uint8_t st;

    msg_toggle[port] ^= LCP_ST_MSGID;
    st = msg_toggle[port];
    n = lcp_build(req, to, LCP_HOST_NODE, st, data, len);
    r = lcr_transport(port, req, n, raw);
    if(r == 0U || !lcp_parse(raw, r, rsp) || !(rsp->status & LCP_ST_RESPONSE) ||
       rsp->to != LCP_HOST_NODE || (rsp->status & LCP_ST_MSGID) != st) {
        return -1;
    }
    return rsp->len;
}

static int32_t be32(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}

void lcr_host_init(void)
{
    int p;
    lcp_frame_t f;
    uint8_t sync_req = 0x00U;

    memset(lcr_port, 0, sizeof(lcr_port));
    /* meter 1: 12 345.6 gal on the totalizer, pump 60.0 gal/min; meter 2: 49 282.7 gal, 45.0 gal/min */
    meter_init(&sim_meter[0], 1U, 123456, 600, 0x1234U);
    meter_init(&sim_meter[1], 2U, 492827, 450, 0x9876U);
    lcr_port[0].node = 1U;
    lcr_port[1].node = 2U;
    for(p = 0; p < LCR_PORTS; p++) {
        lcr_port[p].last_cmd = MCMD_NONE;
        /* session start: Get Product ID with the sync bit (Leo: 7E 7E dd 14 02 01 00 ..) */
        msg_toggle[p] = 1U;
        {
            static uint8_t req[16], raw[64];
            uint32_t n = lcp_build(req, lcr_port[p].node, LCP_HOST_NODE, LCP_ST_SYNC, &sync_req, 1U);
            uint32_t r = lcr_transport(p, req, n, raw);
            lcr_port[p].online = (r && lcp_parse(raw, r, &f)) ? 1U : 0U;
        }
    }
}

int lcr_port_of_node(uint8_t node)
{
    int p;

    if(node == 0U) {
        return -1;
    }
    for(p = 0; p < LCR_PORTS; p++) {
        if(lcr_port[p].node == node) {
            return p;
        }
    }
    return -1;
}

static void hist_add(int port)
{
    lcr_port_t *lp = &lcr_port[port];
    hist_rec_t *h = &lp->hist[(lp->hist_head + lp->hist_count) % HIST_CAP];

    if(lp->hist_count == HIST_CAP) {
        lp->hist_head = (uint16_t)((lp->hist_head + 1U) % HIST_CAP);   /* ring: drop oldest */
        h = &lp->hist[(lp->hist_head + lp->hist_count - 1U) % HIST_CAP];
    } else {
        lp->hist_count++;
    }
    h->ts = lp->ts;
    memcpy(h->v, lp->v, sizeof(h->v));
    h->serial = lp->hist_serial++;
}

void lcr_host_poll(void)
{
    int p, i, changed;
    lcp_frame_t f;
    uint8_t req[2];

    for(p = 0; p < LCR_PORTS; p++) {
        lcr_port_t *lp = &lcr_port[p];
        int ok = 1;

        if(lp->node == 0U) {
            lp->online = 0U;
            continue;
        }
        for(i = 0; i < 6 && ok; i++) {
            int tries;
            req[0] = 0x20U;
            req[1] = poll_fields[i];
            ok = 0;
            for(tries = 0; tries < 2 && !ok; tries++) {     /* one retry on a lost/garbled frame */
                if(lcr_xfer(p, lp->node, req, 2U, &f) == 6 && f.data[0] == 0U) {
                    lp->dev_status = f.data[1];
                    lp->v[i] = be32(&f.data[2]);
                    ok = 1;
                }
            }
        }
        if(!ok) {
            /* offline only after LCR_OFFLINE_POLLS failed polls in a row; until then keep the last
               good values so a single glitch on the wire doesn't drop the meter mid-delivery */
            lp->polls_fail++;
            if(lp->miss < 255U) {
                lp->miss++;
            }
            if(lp->miss >= LCR_OFFLINE_POLLS) {
                lp->online = 0U;
            }
            continue;
        }
        lp->miss = 0U;
        lp->online = 1U;
        lp->polls_ok++;
        lp->serial++;
        lp->ts = app_time();
        /* store a history record when the meter data changed (Leo: bIsRpt on change) */
        changed = memcmp(lp->v, lp->v_old, sizeof(lp->v)) != 0;
        if(changed) {
            hist_add(p);
            memcpy(lp->v_old, lp->v, sizeof(lp->v));
        }
    }
}

/* A command that gets no answer is sent again, up to LCR_CMD_RETRIES more times (V2.89 logs these as
 * "--applcrStopCmdTimeOut--<node>--<cmd>--<n>"). */
#define LCR_CMD_RETRIES 3

static int lcr_xfer_retry(int port, uint8_t to, const uint8_t *data, uint8_t len, lcp_frame_t *rsp)
{
    int n = -1, i;

    for(i = 0; i <= LCR_CMD_RETRIES && n < 2; i++) {
        n = lcr_xfer(port, to, data, len, rsp);
    }
    return n;
}

int lcr_issue(int port, uint8_t cmd)
{
    lcp_frame_t f;
    uint8_t req[2] = {0x24U, cmd};

    if(lcr_xfer_retry(port, lcr_port[port].node, req, 2U, &f) < 2) {
        return -1;
    }
    lcr_port[port].dev_status = f.data[1];
    return f.data[0];
}

static int set_volume_field(int port, uint8_t fld, int32_t v)
{
    lcp_frame_t f;
    uint8_t req[6] = {0x21U, fld, (uint8_t)((uint32_t)v >> 24), (uint8_t)((uint32_t)v >> 16),
                      (uint8_t)((uint32_t)v >> 8), (uint8_t)v};

    if(lcr_xfer_retry(port, lcr_port[port].node, req, 6U, &f) < 2) {
        return -1;
    }
    return f.data[0];
}

/* 20h Get Field Data: copies the field bytes to out, returns their count or -1 (no answer / rc != 0) */
int lcr_get_field(int port, uint8_t fld, uint8_t *out, int max)
{
    lcp_frame_t f;
    uint8_t req[2] = {0x20U, fld};
    int n = lcr_xfer_retry(port, lcr_port[port].node, req, 2U, &f);

    if(n < 2 || f.data[0] != 0U) {
        return -1;
    }
    lcr_port[port].dev_status = f.data[1];
    n -= 2;
    if(n > max) {
        n = max;
    }
    memcpy(out, &f.data[2], (uint32_t)n);
    return n;
}

int lcr_set_preset(int port, int32_t tenths)
{
    return set_volume_field(port, 5U, tenths);
}

int lcr_set_net_preset(int port, int32_t tenths)
{
    return set_volume_field(port, 6U, tenths);
}

int lcr_set_address(int port, uint8_t old_node, uint8_t new_node)
{
    lcp_frame_t f;
    uint8_t req[2] = {0x25U, new_node};

    if(lcr_xfer(port, old_node, req, 2U, &f) < 2) {
        return -1;
    }
    return f.data[0];
}

int lcr_delivery_status(int port, uint8_t *dev, uint16_t *dstat, uint16_t *dcode)
{
    lcp_frame_t f;
    uint8_t req[1] = {0x28U};

    if(lcr_xfer(port, lcr_port[port].node, req, 1U, &f) < 6) {
        return -1;
    }
    *dev = f.data[1];
    *dstat = (uint16_t)((f.data[2] << 8) | f.data[3]);
    *dcode = (uint16_t)((f.data[4] << 8) | f.data[5]);
    return f.data[0];
}

int lcr_find_node(int port, uint8_t from, uint8_t to)
{
    lcp_frame_t f;
    uint8_t req[1] = {0x28U};
    uint32_t a;

    for(a = from; a <= to && a <= 250U; a++) {
        if(lcr_xfer(port, (uint8_t)a, req, 1U, &f) >= 2) {
            return (int)a;
        }
    }
    return 0;
}

void hist_clear(int port)
{
    lcr_port[port].hist_head = 0U;
    lcr_port[port].hist_count = 0U;
}

uint16_t hist_count(int port)
{
    return lcr_port[port].hist_count;
}

const hist_rec_t *hist_get(int port, uint16_t i)
{
    lcr_port_t *lp = &lcr_port[port];

    if(i >= lp->hist_count) {
        return NULL;
    }
    return &lp->hist[(lp->hist_head + i) % HIST_CAP];
}

void hist_drop_oldest(int port)
{
    lcr_port_t *lp = &lcr_port[port];

    if(lp->hist_count) {
        lp->hist_head = (uint16_t)((lp->hist_head + 1U) % HIST_CAP);
        lp->hist_count--;
    }
}
