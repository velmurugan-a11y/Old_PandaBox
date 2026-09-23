/*!
    \file    app.h
    \brief   PandaBox application: time base, box configuration, command protocol
*/

#ifndef APP_H
#define APP_H

#include <stdint.h>

#define FW_HW_VER       "2.4"
#define FW_HW_DATE      "250502"
#define FW_SW_VER       "3.001"         /* new firmware line (Leo's last is 2.891) */

uint32_t millis(void);
void     delay_ms(uint32_t ms);

uint32_t app_time(void);                /* unix epoch seconds */
void     app_set_time(uint32_t epoch);

typedef void (*reply_fn_t)(const char *line);

/* handle one received command line; replies go through out() (one call per line) */
void     proto_handle(const char *line, reply_fn_t out);
void     proto_init(void);
void     proto_tick(void);              /* deferred actions (reset, BT rename) */

void     app_log_bt_event(const uint8_t *e, uint32_t n);
const char *app_imei(void);
void     app_set_imei(const char *imei);
const char *app_bt_name(void);
const char *app_build_date(void);    /* YYMMDD */

#endif /* APP_H */
