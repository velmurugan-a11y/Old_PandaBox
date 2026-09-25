/*
 * sys.c - DBG, TMR, EVT (V2.89 sys.c / dbg: 0x08009E70, 0x08009EBC, 0x0800B61C).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "sys.h"

/* ------------------------------------------------------------------ DBG */
static uint8_t s_dbg_on = 1;
static uint8_t s_dbg_level = DBG_D;
static char s_dbg_buf[300];

static const char *const s_level_name[DBG_LEVEL_MAX] = {"DEBUG", "INFO", "WARNING", "ERROR", "ASSERT"};

const char *DBG_LevelName(uint8_t level)
{
    return level < DBG_LEVEL_MAX ? s_level_name[level] : "?";
}

void DBG_Init(uint8_t on)
{
    s_dbg_on = on;
    s_dbg_level = DBG_D;
    HAL_UartConfig(E_HAL_UART_PRINT, 115200U, 0, 0);
    HAL_UartSetPackInterval(E_HAL_UART_PRINT, 50U);
}

void DBG_SetOn(uint8_t on)
{
    s_dbg_on = on;
}

uint8_t DBG_IsOn(void)
{
    return s_dbg_on;
}

int DBG_SetPrintLevel(uint8_t level)
{
    if(level >= DBG_LEVEL_MAX) {
        return 71;
    }
    s_dbg_level = level;
    s_dbg_on = 1;
    return 0;
}

uint8_t DBG_GetPrintLevel(void)
{
    return s_dbg_level;
}

static const char *basename_of(const char *path)
{
    const char *b = path;
    for(; *path; path++) {
        if(*path == '/' || *path == '\\') {
            b = path + 1;
        }
    }
    return b;
}

int DBG_Print(uint8_t level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    int n;

    if(!s_dbg_on) {
        return 70;
    }
    if(level >= DBG_LEVEL_MAX || level < s_dbg_level) {
        return 71;
    }
    n = snprintf(s_dbg_buf, sizeof(s_dbg_buf), "\r\n%c,%lu,%s,%04d:", s_level_name[level][0],
                 (unsigned long)HAL_GetTick(), basename_of(file), line);
    if(n < 0) {
        return 72;
    }
    va_start(ap, fmt);
    vsnprintf(s_dbg_buf + n, sizeof(s_dbg_buf) - (size_t)n, fmt, ap);
    va_end(ap);
    return HAL_UartSend(E_HAL_UART_PRINT, s_dbg_buf, (uint16_t)strlen(s_dbg_buf));
}

int DBG_PrintRaw(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_dbg_buf, sizeof(s_dbg_buf), fmt, ap);
    va_end(ap);
    return HAL_UartSend(E_HAL_UART_PRINT, s_dbg_buf, (uint16_t)strlen(s_dbg_buf));
}

int DBG_SendRaw(const void *data, uint16_t len)
{
    return HAL_UartSend(E_HAL_UART_PRINT, data, len);
}

void ASSERT_Fail(const char *file, int line)
{
    char b[100];
    snprintf(b, sizeof(b), "\r\nASSERT: %s,%04d", basename_of(file), line);
    HAL_UartSend(E_HAL_UART_PRINT, b, (uint16_t)strlen(b));
    HAL_UartFlush(E_HAL_UART_PRINT);
    for(;;) {
        /* like Leo: hang until the watchdog resets the board */
    }
}

/* ------------------------------------------------------------------ TMR */
typedef struct {
    uint8_t used;
    uint8_t running;
    uint8_t repeat;
    uint32_t period;
    uint32_t start;
    TMR_CB cb;
    void *arg;
} T_TMR;

static T_TMR s_tmr[TMR_NUM_MAX];

static int tmr_new(uint32_t ms, TMR_CB cb, void *arg, uint8_t *id, uint8_t repeat)
{
    uint8_t i;

    for(i = 0; i < TMR_NUM_MAX; i++) {
        if(!s_tmr[i].used) {
            memset(&s_tmr[i], 0, sizeof(s_tmr[i]));
            s_tmr[i].used = 1;
            s_tmr[i].repeat = repeat;
            s_tmr[i].period = ms;
            s_tmr[i].cb = cb;
            s_tmr[i].arg = arg;
            *id = i;
            return 0;
        }
    }
    return 1;
}

int TMR_Creat(uint32_t ms, TMR_CB cb, void *arg, uint8_t *id)
{
    return tmr_new(ms, cb, arg, id, 0);
}

int TMR_CreatRepeatTimer(uint32_t ms, TMR_CB cb, void *arg, uint8_t *id)
{
    return tmr_new(ms, cb, arg, id, 1);
}

int TMR_Start(uint8_t id)
{
    if(id >= TMR_NUM_MAX || !s_tmr[id].used) {
        return 1;
    }
    if(!s_tmr[id].running) {
        s_tmr[id].running = 1;
        s_tmr[id].start = HAL_GetTick();
    }
    return 0;
}

int TMR_Stop(uint8_t id)
{
    if(id >= TMR_NUM_MAX || !s_tmr[id].used) {
        return 1;
    }
    s_tmr[id].running = 0;
    return 0;
}

int TMR_Restart(uint8_t id)
{
    if(id >= TMR_NUM_MAX || !s_tmr[id].used) {
        return 1;
    }
    s_tmr[id].running = 1;
    s_tmr[id].start = HAL_GetTick();
    return 0;
}

int TMR_SetPeriod(uint8_t id, uint32_t ms)
{
    if(id >= TMR_NUM_MAX || !s_tmr[id].used) {
        return 1;
    }
    s_tmr[id].period = ms;
    return 0;
}

int TMR_Kill(uint8_t id)
{
    if(id >= TMR_NUM_MAX) {
        return 1;
    }
    s_tmr[id].used = 0;
    s_tmr[id].running = 0;
    return 0;
}

int TMR_IsStarted(uint8_t id, uint8_t *started)
{
    if(id >= TMR_NUM_MAX || !s_tmr[id].used) {
        return 1;
    }
    *started = s_tmr[id].running;
    return 0;
}

void TMR_ProcessTimeout(void)
{
    uint8_t i;
    uint32_t now = HAL_GetTick();

    for(i = 0; i < TMR_NUM_MAX; i++) {
        T_TMR *t = &s_tmr[i];
        if(t->used && t->running && (now - t->start) >= t->period) {
            if(t->repeat) {
                t->start = now;
            } else {
                t->running = 0;
            }
            if(t->cb) {
                t->cb(t->arg);
            }
        }
    }
}

/* ------------------------------------------------------------------ EVT */
typedef struct {
    uint8_t used;
    volatile uint8_t pending;
    uint32_t arg;
    EVT_CB cb;
} T_EVT;

static T_EVT s_evt[EVT_NUM_MAX];

int EVT_Creat(EVT_CB cb, uint8_t *id)
{
    uint8_t i;

    for(i = 0; i < EVT_NUM_MAX; i++) {
        if(!s_evt[i].used) {
            s_evt[i].used = 1;
            s_evt[i].pending = 0;
            s_evt[i].cb = cb;
            *id = i;
            return 0;
        }
    }
    return 1;
}

int EVT_PostEvent(uint8_t id, uint32_t arg)
{
    if(id >= EVT_NUM_MAX || !s_evt[id].used) {
        return 1;
    }
    s_evt[id].arg = arg;
    s_evt[id].pending = 1;
    return 0;
}

void EVT_DoEvent(void)
{
    uint8_t i;

    for(i = 0; i < EVT_NUM_MAX; i++) {
        if(s_evt[i].used && s_evt[i].pending) {
            s_evt[i].pending = 0;
            if(s_evt[i].cb) {
                s_evt[i].cb(s_evt[i].arg);
            }
        }
    }
}

/* ------------------------------------------------------------------ SYS */
void SYS_Init(void)
{
    memset(s_tmr, 0, sizeof(s_tmr));
    memset(s_evt, 0, sizeof(s_evt));
    DBG_Init(1);
    DBG(DBG_I, "System reboot, reason: %s", HAL_GetResetReasonStr());
}

void SYS_Reboot(void)
{
    HAL_UartFlush(E_HAL_UART_PRINT);
    HAL_Reboot();
}

void SYS_DelayMs(uint32_t ms)
{
    HAL_DelayMs(ms);
}
