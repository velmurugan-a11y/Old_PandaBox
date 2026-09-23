/*!
    \file    bt.h
    \brief   Yichip YC1021 Bluetooth module on UART3 (PC10/PC11, 115200 8N1)

    Start-up (from Leo's V2.87/V2.89 app): HCI upload of 105 records (FC03 patch, FC10 memory writes,
    FC04 start), then Yichip commands 01 <cmd> <len> <data>, acknowledged with 02 06 02 <cmd> <status>:
      03 BT name, 04 BLE name, 0C pairing mode, 0D PIN, 00 BT address, 01 BLE address, 02 visibility.
    Data:  phone -> box  02 08 <len> 2D 00 <bytes>   (BLE write to handle 0x002D)
                         02 07 <len> <bytes>         (SPP)
           box -> phone  01 09 <len> 2A 00 <bytes>   (BLE notify on handle 0x002A, <= 125 bytes)
                         01 05 <len> <bytes>         (SPP)
*/

#ifndef BT_H
#define BT_H

#include <stdint.h>

typedef enum { BT_LINK_BLE = 0, BT_LINK_SPP = 1 } bt_link_t;

int      bt_start(const char *name);        /* full bring-up; returns 1 when all steps acknowledged */
int      bt_set_name(const char *name);     /* rename (BT name + "BLE" for LE), keep visible */
void     bt_poll(void);                     /* parse module events, assemble command lines */
void     bt_service(void);                  /* main loop: restart advertising after a link ends */
uint32_t bt_restarts(void);                /* self-heal restarts since boot */
uint8_t  bt_status(void);                   /* last 02 02 status byte (0xFF = none yet) */
int      bt_get_line(char *out, uint32_t max, bt_link_t *link);   /* 1 when a full command line is ready */
void     bt_send_line(bt_link_t link, const char *line);         /* appends \r\n, chunks, waits for acks */
uint8_t  bt_is_up(void);
uint32_t bt_rx_bytes(void);
uint32_t bt_tx_frames(void);

extern uint8_t bt_trace;                    /* print raw module frames on the console */

#endif /* BT_H */
