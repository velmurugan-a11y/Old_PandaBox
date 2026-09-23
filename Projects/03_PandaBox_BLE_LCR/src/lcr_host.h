/*!
    \file    lcr_host.h
    \brief   PandaBox side of the LCR link: two ports, LCP polling, commands, history records
*/

#ifndef LCR_HOST_H
#define LCR_HOST_H

#include <stdint.h>

#define LCR_PORTS       2
#define HIST_CAP        400     /* records per port, RAM only (external flash left untouched) */

typedef enum { MCMD_NONE = 0, MCMD_START, MCMD_STOP, MCMD_PAUSE, MCMD_PRINT } mcmd_t;

typedef struct {
    uint32_t ts;
    int32_t  v[6];              /* #2 gross, #4 flow, #17 gross total, #18 net total, #100, #101 */
    uint8_t  serial;
} hist_rec_t;

typedef struct {
    uint8_t  node;              /* SetPortLcrNode: node the box expects on this port (0 = none) */
    uint8_t  online;            /* last poll answered */
    uint8_t  serial;            /* poll counter 0..255 */
    uint32_t ts;                /* time of last complete sample */
    int32_t  v[6];
    int32_t  v_old[6];
    uint8_t  dev_status;
    mcmd_t   last_cmd;
    uint8_t  last_cmd_rc;
    uint32_t polls_ok, polls_fail;
    /* history ring */
    hist_rec_t hist[HIST_CAP];
    uint16_t hist_head, hist_count;
    uint8_t  hist_serial;
} lcr_port_t;

extern lcr_port_t lcr_port[LCR_PORTS];

void     lcr_host_init(void);
void     lcr_host_poll(void);                       /* call every ~1 s */
int      lcr_port_of_node(uint8_t node);            /* -1 if not configured */

/* LCP transactions; return rc (0..255) or -1 when the meter did not answer */
int      lcr_issue(int port, uint8_t cmd);           /* 24h */
int      lcr_set_preset(int port, int32_t tenths);   /* 21h #5 */
int      lcr_set_net_preset(int port, int32_t tenths);
int      lcr_set_address(int port, uint8_t old_node, uint8_t new_node);   /* 25h */
int      lcr_find_node(int port, uint8_t from, uint8_t to);              /* 28h scan, returns node or 0 */
int      lcr_delivery_status(int port, uint8_t *dev, uint16_t *dstat, uint16_t *dcode);

/* history */
void     hist_clear(int port);
uint16_t hist_count(int port);
const hist_rec_t *hist_get(int port, uint16_t i);   /* 0 = oldest */
void     hist_drop_oldest(int port);

/* simulated meter on a port (for the console / physics tick) */
struct meter_s;

/* debug: print LCP frames on the console */
extern uint8_t lcr_trace;

#endif /* LCR_HOST_H */
