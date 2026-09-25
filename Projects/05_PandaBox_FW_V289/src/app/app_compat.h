/*
 * app_compat.h - glue so the proven LCP/meter/host/proto sources from Projects/03 build against
 * this project's HAL/SYS. Same behaviour, just different plumbing.
 */
#ifndef APP_COMPAT_H
#define APP_COMPAT_H

#include <stdint.h>
#include "gd32f30x.h"
#include "hal.h"
#include "version.h"

typedef void (*reply_fn_t)(const char *line);

/* time base -> SysTick / RTC */
uint32_t millis(void);
void     delay_ms(uint32_t ms);
uint32_t app_time(void);
void     app_set_time(uint32_t epoch);
const char *app_build_date(void);
const char *app_bt_name(void);
void     bt_set_name(const char *name);

/* LxBoxInfo version fields */
#define FW_HW_VER   HW_VER_STR
#define FW_HW_DATE  HW_DATE_STR
#define FW_SW_VER   FW_VER_STR

/* proto.c entry points */
void proto_init(void);
void proto_handle(const char *line, reply_fn_t out);
void proto_tick(void);

#endif
