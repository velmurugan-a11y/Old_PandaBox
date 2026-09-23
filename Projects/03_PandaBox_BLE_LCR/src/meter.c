/*!
    \file    meter.c
    \brief   simulated Liquid Controls LCR-II register (see docs/LCR_Simulation.md)

    Behaviour follows the LCP API document (Revision L) and the LCR-II manuals:
    - Issue Command 24h: 0 start/resume, 1 pause, 2 end delivery (+ticket), 6 print
    - #17/#18 totalizers count live with flow; #100/#101 hold them from the start of the last delivery
    - pause and end close the valve; flow runs down over ~1-2 s (state WAIT_NO_FLOW when ending)
    - gross preset (#5): delivery ends (Clear) when reached
    - no-flow timer (#25): a paused delivery ends after #25 seconds without flow
*/

#include <string.h>
#include "meter.h"

/* return codes */
#define RC_OK               0U
#define RC_INVALID_FIELD    33U
#define RC_BAD_DATA         34U
#define RC_NOT_SET_MODE     35U
#define RC_INVALID_CMD      36U
#define RC_INVALID_ADDR     37U
#define RC_STATE            120U
#define RC_TICKET_PENDING   121U

#define FLOW_TAU_OPEN_MS    1500    /* ramp up after the valve opens */
#define FLOW_TAU_CLOSE_MS   700     /* run-down after the valve closes */

static uint32_t rnd(meter_t *m)
{
    m->sim_seed = m->sim_seed * 1664525U + 1013904223U;
    return m->sim_seed >> 8;
}

void meter_init(meter_t *m, uint8_t node, int32_t gross_total, int32_t target_flow, uint32_t seed)
{
    memset(m, 0, sizeof(*m));
    m->node = node;
    m->product_id = "SR200b2.05";      /* LCR-II example from the LCP document */
    m->state = MS_END;
    m->gross_total = gross_total;
    m->prev_gross = gross_total;
    m->sale_no = 25U;
    m->ticket_no = 1U;
    m->noflow_s = 180U;
    m->preset_type = 0U;                /* Clear */
    m->ticket_req = 1U;                 /* print if printer available: no printer -> never blocks */
    m->decimals = 1U;
    m->target_flow = target_flow;
    m->sim_seed = seed;
}

uint8_t meter_dev_status(const meter_t *m)
{
    return (uint8_t)(m->state | SW_RUN);
}

static void end_delivery(meter_t *m)
{
    m->state = MS_END;
    m->flow = 0;
    m->paused = 0U;
    m->del_code &= (uint16_t)~(DC_DELIVERY_ACTIVE | DC_FLOW_ACTIVE | DC_GROSS_PRESET);
    m->del_status &= (uint16_t)~DS_STOP_REQUEST;
    if(m->ticket_req == 0U) {
        m->ticket_pending = 1U;
        m->del_code |= DC_TICKET_PENDING;
    } else {
        m->ticket_no++;                 /* printed straight away */
    }
    if(m->preset_type == 0U) {
        m->preset = 0;                  /* Clear preset type */
    }
}

void meter_tick(meter_t *m, uint32_t dt_ms)
{
    int32_t target, delta;
    int64_t add;

    if(m->state == MS_END) {
        return;
    }
    /* valve open while running and not paused */
    target = (m->state == MS_RUN || (m->state == MS_STOP && !m->paused)) ? m->target_flow : 0;
    if(target > 0) {
        /* +-2 % pump variation */
        target += (int32_t)(rnd(m) % (uint32_t)(target / 25 + 1)) - target / 50;
    }
    delta = target - m->flow;
    m->flow += (int32_t)((int64_t)delta * (int64_t)dt_ms /
                         (int64_t)((target > m->flow) ? FLOW_TAU_OPEN_MS : FLOW_TAU_CLOSE_MS));
    if(target == 0 && m->flow < 5) {
        m->flow = 0;                    /* below 0.5 gal/min: no flow */
    }
    if(m->flow < 0) {
        m->flow = 0;
    }

    /* integrate volume: flow [0.1 gal/min] * dt [ms] -> 0.1 gal * 1000 */
    add = (int64_t)m->flow * (int64_t)dt_ms / 60;
    m->acc_milli += add;
    if(m->acc_milli >= 1000) {
        int32_t units = (int32_t)(m->acc_milli / 1000);
        m->acc_milli -= (int64_t)units * 1000;
        m->gross += units;
        m->gross_total += units;
    }

    /* delivery code / machine state */
    if(m->flow > 0) {
        m->del_code |= DC_FLOW_ACTIVE;
    } else {
        m->del_code &= (uint16_t)~DC_FLOW_ACTIVE;
    }
    if(m->state == MS_WAIT_NOFLOW) {
        if(m->flow == 0) {
            end_delivery(m);
        }
        return;
    }
    m->state = (m->flow > 0) ? MS_RUN : MS_STOP;

    /* gross preset reached: close valve and end (Clear) */
    if(m->preset > 0 && m->gross >= m->preset && !m->paused) {
        m->del_status |= DS_PRESET_REACHED;
        m->del_code |= DC_GROSS_PRESET_HIT;
        m->state = MS_WAIT_NOFLOW;
        return;
    }

    /* no-flow timer while paused */
    if(m->paused && m->flow == 0) {
        m->noflow_ms += dt_ms;
        if(m->noflow_s && m->noflow_ms >= (uint32_t)m->noflow_s * 1000U && m->gross >= 10) {
            m->del_status |= DS_NOFLOW_STOP;
            end_delivery(m);
        }
    } else {
        m->noflow_ms = 0U;
    }
}

static uint8_t issue_command(meter_t *m, uint8_t cmd)
{
    switch(cmd) {
    case 0U:    /* start / resume */
        if(m->state == MS_END) {
            if(m->ticket_pending && m->ticket_req == 0U) {
                return RC_TICKET_PENDING;
            }
            m->sale_no++;
            m->prev_gross = m->gross_total;
            m->prev_net = m->net_total;
            m->gross = 0;
            m->acc_milli = 0;
            m->del_status = 0U;
            m->del_code = DC_DELIVERY_ACTIVE | ((m->preset > 0) ? DC_GROSS_PRESET : 0U);
            m->paused = 0U;
            m->state = MS_STOP;         /* valve opening, no flow yet */
            return RC_OK;
        }
        if(m->state == MS_WAIT_NOFLOW) {
            return RC_STATE;
        }
        m->paused = 0U;                 /* resume (no-op if running) */
        m->del_status &= (uint16_t)~DS_STOP_REQUEST;
        return RC_OK;
    case 1U:    /* pause */
        if(m->state == MS_END) {
            return RC_STATE;
        }
        m->paused = 1U;
        m->del_status |= DS_STOP_REQUEST;
        return RC_OK;
    case 2U:    /* end delivery (+ticket) */
        if(m->state == MS_END) {
            return RC_OK;               /* nothing to end */
        }
        m->del_status |= DS_END_REQUEST;
        m->del_status &= (uint16_t)~DS_STOP_REQUEST;
        m->paused = 0U;
        m->state = MS_WAIT_NOFLOW;
        if(m->flow == 0) {
            end_delivery(m);
        }
        return RC_OK;
    case 6U:    /* print ticket for current state */
        if(m->state != MS_END) {
            return RC_STATE;
        }
        if(m->ticket_pending) {
            m->ticket_pending = 0U;
            m->del_code &= (uint16_t)~DC_TICKET_PENDING;
        }
        m->ticket_no++;
        return RC_OK;
    case 3U:
    case 4U:
        return RC_OK;
    default:
        return RC_INVALID_CMD;
    }
}

static uint32_t put32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)((uint32_t)v >> 24);
    p[1] = (uint8_t)((uint32_t)v >> 16);
    p[2] = (uint8_t)((uint32_t)v >> 8);
    p[3] = (uint8_t)v;
    return 4U;
}

static int32_t get32(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}

/* field read: returns data length, 0 = invalid field */
static uint32_t field_get(const meter_t *m, uint16_t fld, uint8_t *d)
{
    switch(fld) {
    case 2U:   return put32(d, m->gross);
    case 3U:   return put32(d, 0);
    case 4U:   return put32(d, m->flow);
    case 5U:   return put32(d, m->preset);
    case 6U:   return put32(d, m->net_preset);
    case 17U:  return put32(d, m->gross_total);
    case 18U:  return put32(d, m->net_total);
    case 22U:  return put32(d, (int32_t)m->sale_no);
    case 23U:  return put32(d, (int32_t)m->ticket_no);
    case 25U:  d[0] = (uint8_t)(m->noflow_s >> 8); d[1] = (uint8_t)m->noflow_s; return 2U;
    case 27U:  d[0] = m->preset_type; return 1U;
    case 37U:  d[0] = m->ticket_req; return 1U;
    case 38U:  d[0] = 0U; return 1U;               /* gallons */
    case 39U:  d[0] = m->decimals; return 1U;
    case 44U:  return put32(d, (m->gross > 0) ? m->gross : 0);
    case 45U:  return put32(d, 0);
    case 92U:  return put32(d, (m->preset > m->gross) ? (m->preset - m->gross) : 0);
    case 100U: return put32(d, m->prev_gross);
    case 101U: return put32(d, m->prev_net);
    case 102U: d[0] = 0U; d[1] = m->node; return 2U;
    default:   return 0U;
    }
}

/* field write: returns rc */
static uint8_t field_set(meter_t *m, uint16_t fld, const uint8_t *d, uint32_t n)
{
    int32_t v;

    switch(fld) {
    case 5U:
    case 6U:
        if(n != 4U) {
            return RC_BAD_DATA;
        }
        if(m->state == MS_RUN || m->state == MS_WAIT_NOFLOW) {
            return RC_NOT_SET_MODE;     /* not editable while product is flowing */
        }
        v = get32(d);
        if(v < 0) {
            return 113U;                /* range check */
        }
        if(fld == 5U) {
            m->preset = v;
            if(m->state != MS_END) {
                m->del_code = (uint16_t)((m->del_code & ~DC_GROSS_PRESET) | (v > 0 ? DC_GROSS_PRESET : 0U));
            }
        } else {
            m->net_preset = v;
        }
        return RC_OK;
    case 25U:
        if(n != 2U) {
            return RC_BAD_DATA;
        }
        m->noflow_s = (uint16_t)((d[0] << 8) | d[1]);
        return RC_OK;
    case 27U:
        m->preset_type = d[0];
        return RC_OK;
    case 37U:
        m->ticket_req = d[0];
        return RC_OK;
    case 102U:
        if(n != 2U || d[1] == 0U || d[1] > 250U) {
            return RC_INVALID_ADDR;
        }
        m->node = d[1];
        return RC_OK;
    case 2U: case 3U: case 4U: case 17U: case 18U: case 100U: case 101U:
        return 117U;                    /* set never allowed */
    default:
        return RC_INVALID_FIELD;
    }
}

uint32_t meter_lcp(meter_t *m, const uint8_t *req, uint32_t n, uint8_t *rsp)
{
    lcp_frame_t f;
    uint8_t d[64];
    uint32_t len = 0U, k;
    uint8_t old_node = m->node;

    if(!lcp_parse(req, n, &f) || (f.status & LCP_ST_RESPONSE) || f.len == 0U) {
        return 0U;
    }
    if(f.to != m->node) {
        return 0U;                      /* not addressed to us (0 = broadcast, no reply) */
    }
    m->rx_frames++;
    switch(f.data[0]) {
    case 0x00U:     /* Get Product ID: rc, product 02 = LCR, ASCIIZ name */
        d[len++] = RC_OK;
        d[len++] = 0x02U;
        k = (uint32_t)strlen(m->product_id);
        memcpy(&d[len], m->product_id, k + 1U);
        len += k + 1U;
        break;
    case 0x20U:     /* Get Field Data */
    case 0x40U:     /* Get Extended Field Data */
        {
            uint16_t fld = (f.data[0] == 0x20U) ? f.data[1] : (uint16_t)((f.data[1] << 8) | f.data[2]);
            d[1] = meter_dev_status(m);
            k = field_get(m, fld, &d[2]);
            d[0] = k ? RC_OK : RC_INVALID_FIELD;
            len = 2U + k;
        }
        break;
    case 0x21U:     /* Set Field Data */
    case 0x41U:
        {
            uint32_t hdr = (f.data[0] == 0x21U) ? 2U : 3U;
            uint16_t fld = (hdr == 2U) ? f.data[1] : (uint16_t)((f.data[1] << 8) | f.data[2]);
            d[0] = (f.len > hdr) ? field_set(m, fld, &f.data[hdr], f.len - hdr) : RC_BAD_DATA;
            d[1] = meter_dev_status(m);
            len = 2U;
        }
        break;
    case 0x23U:     /* Get Machine Status: rc dev prn delStatus delCode */
        d[0] = RC_OK;
        d[1] = meter_dev_status(m);
        d[2] = 0x20U;                   /* no print processor online */
        d[3] = (uint8_t)(m->del_status >> 8); d[4] = (uint8_t)m->del_status;
        d[5] = (uint8_t)(m->del_code >> 8);   d[6] = (uint8_t)m->del_code;
        len = 7U;
        break;
    case 0x24U:     /* Issue Command */
        d[0] = (f.len >= 2U) ? issue_command(m, f.data[1]) : RC_INVALID_CMD;
        d[1] = meter_dev_status(m);
        len = 2U;
        break;
    case 0x25U:     /* Set Device Address (reply comes from the old address) */
        if(f.len >= 2U && f.data[1] >= 1U && f.data[1] <= 250U) {
            m->node = f.data[1];
            d[0] = RC_OK;
        } else {
            d[0] = RC_INVALID_ADDR;
        }
        d[1] = meter_dev_status(m);
        len = 2U;
        break;
    case 0x26U:     /* Get Version Number */
        d[0] = RC_OK; d[1] = meter_dev_status(m); d[2] = 1U; d[3] = 0U;
        len = 4U;
        break;
    case 0x27U:     /* Get Security Level: 0x01 locked idle, 0x80 flag while delivering */
        d[0] = RC_OK; d[1] = meter_dev_status(m);
        d[2] = (m->state == MS_END) ? 0x01U : (m->paused ? 0x00U : 0x81U);
        len = 3U;
        break;
    case 0x28U:     /* Get Delivery Status: rc dev delStatus delCode */
        d[0] = RC_OK;
        d[1] = meter_dev_status(m);
        d[2] = (uint8_t)(m->del_status >> 8); d[3] = (uint8_t)m->del_status;
        d[4] = (uint8_t)(m->del_code >> 8);   d[5] = (uint8_t)m->del_code;
        len = 6U;
        break;
    default:        /* not supported: status bit 0x40, no data */
        m->tx_frames++;
        return lcp_build(rsp, f.from, old_node, (uint8_t)(LCP_ST_RESPONSE | 0x40U | (f.status & LCP_ST_MSGID)),
                         NULL, 0U);
    }
    m->tx_frames++;
    return lcp_build(rsp, f.from, old_node, (uint8_t)(LCP_ST_RESPONSE | (f.status & LCP_ST_MSGID)), d, (uint8_t)len);
}
