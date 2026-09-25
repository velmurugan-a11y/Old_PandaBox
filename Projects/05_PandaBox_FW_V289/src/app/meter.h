/*!
    \file    meter.h
    \brief   simulated Liquid Controls LCR-II register, answering LCP like the real meter

    Volumes are VOLUME fields: signed 32-bit, tenths of a gallon (#39 Decimals = 1).
    Flow rate #4 is tenths of a gallon per minute.
*/

#ifndef METER_H
#define METER_H

#include <stdint.h>
#include "lcp.h"

/* machine state (devStatus bits 4-6) */
#define MS_RUN          0x00U   /* delivery active, flow */
#define MS_STOP         0x10U   /* delivery active, no flow (paused) */
#define MS_END          0x20U   /* no delivery (idle) */
#define MS_WAIT_NOFLOW  0x60U   /* delivery being ended, flow still decaying */

#define SW_RUN          0x01U   /* switch position (bits 0-2) */

/* delCode */
#define DC_TICKET_PENDING   0x0001U
#define DC_FLOW_ACTIVE      0x0004U
#define DC_DELIVERY_ACTIVE  0x0008U
#define DC_GROSS_PRESET     0x0010U
#define DC_GROSS_PRESET_HIT 0x0040U

/* delStatus */
#define DS_PRESET_REACHED   0x0080U
#define DS_NOFLOW_STOP      0x0100U
#define DS_STOP_REQUEST     0x0200U
#define DS_END_REQUEST      0x0400U

typedef struct {
    uint8_t  node;              /* #102 LCRNode / LCP address */
    const char *product_id;     /* 00h name */
    uint8_t  state;             /* MS_* */
    uint8_t  paused;            /* STOP_REQUEST active */
    uint16_t del_status;
    uint16_t del_code;
    int32_t  gross;             /* #2  current delivery */
    int32_t  flow;              /* #4  tenths gal/min */
    int32_t  preset;            /* #5  gross preset (0 = none) */
    int32_t  net_preset;        /* #6 */
    int32_t  gross_total;       /* #17 live totalizer */
    int32_t  net_total;         /* #18 (0, not compensated) */
    int32_t  prev_gross;        /* #100 #17 at start of last delivery */
    int32_t  prev_net;          /* #101 */
    uint32_t sale_no;           /* #22 */
    uint32_t ticket_no;         /* #23 */
    uint16_t noflow_s;          /* #25 */
    uint8_t  preset_type;       /* #27 0 Clear */
    uint8_t  ticket_req;        /* #37 0 required, 1 if printer, 2 never */
    uint8_t  decimals;          /* #39 1 = tenths */
    uint8_t  ticket_pending;
    int32_t  target_flow;       /* simulated pump rate while valve open */
    uint32_t noflow_ms;         /* time without flow while paused */
    int64_t  acc_milli;         /* delivered volume accumulator, 1/1000 of a tenth */
    uint32_t sim_seed;
    uint32_t rx_frames, tx_frames;
} meter_t;

void    meter_init(meter_t *m, uint8_t node, int32_t gross_total, int32_t target_flow, uint32_t seed);
void    meter_tick(meter_t *m, uint32_t dt_ms);
uint8_t meter_dev_status(const meter_t *m);

/* LCP endpoint: request frame in, response frame out (returns 0 if the frame is not for this meter) */
uint32_t meter_lcp(meter_t *m, const uint8_t *req, uint32_t n, uint8_t *rsp);

#endif /* METER_H */
