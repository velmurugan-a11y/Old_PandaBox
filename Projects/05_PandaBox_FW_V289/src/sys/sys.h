/*
 * sys.h - common services, same API names as Leo's src/sys: DBG, TMR (software timers), EVT (deferred events).
 */
#ifndef SYS_H
#define SYS_H

#include <stdint.h>

/* ---- debug print: "\r\n<L>,<ms>,<file>,<line>:<text>" on USART0 ---- */
typedef enum {
    DBG_D = 0,   /* DEBUG */
    DBG_I,       /* INFO */
    DBG_W,       /* WARNING */
    DBG_E,       /* ERROR */
    DBG_A,       /* ASSERT */
    DBG_LEVEL_MAX
} E_DBG_LEVEL;

void DBG_Init(uint8_t on);
int  DBG_Print(uint8_t level, const char *file, int line, const char *fmt, ...);
int  DBG_PrintRaw(const char *fmt, ...);
int  DBG_SendRaw(const void *data, uint16_t len);
void DBG_SetOn(uint8_t on);
uint8_t DBG_IsOn(void);
int  DBG_SetPrintLevel(uint8_t level);
uint8_t DBG_GetPrintLevel(void);
void DBG_SetTimeOn(uint8_t on);
const char *DBG_LevelName(uint8_t level);
void ASSERT_Fail(const char *file, int line);

/* the GD standard-peripheral library defines DBG as the DBG_BASE peripheral; we use it as our log
 * macro, so drop the library's object-like macro (we never touch the DBG peripheral). */
#ifdef DBG
#undef DBG
#endif
#define DBG(level, ...)  DBG_Print((level), __FILE__, __LINE__, __VA_ARGS__)
/* DBGL: print with the line number Leo's V2.89 prints for the same message, so our console trace
 * lines up with his ("D,<ms>,app_lcr.c,2290:..."). The number is the one found in the binary. */
#define DBGL(level, line, ...)  DBG_Print((level), __FILE__, (line), __VA_ARGS__)
#define ASSERT(x)        do { if(!(x)) { ASSERT_Fail(__FILE__, __LINE__); } } while(0)

/* ---- software timers ---- */
#define TMR_NUM_MAX 40
typedef void (*TMR_CB)(void *arg);
int  TMR_Creat(uint32_t ms, TMR_CB cb, void *arg, uint8_t *id);            /* one-shot */
int  TMR_CreatRepeatTimer(uint32_t ms, TMR_CB cb, void *arg, uint8_t *id); /* periodic */
int  TMR_Start(uint8_t id);
int  TMR_Stop(uint8_t id);
int  TMR_Restart(uint8_t id);
int  TMR_SetPeriod(uint8_t id, uint32_t ms);
int  TMR_Kill(uint8_t id);
int  TMR_IsStarted(uint8_t id, uint8_t *started);
void TMR_ProcessTimeout(void);

/* ---- deferred events: callback runs on the next main-loop pass ---- */
#define EVT_NUM_MAX 24
typedef void (*EVT_CB)(uint32_t arg);
int  EVT_Creat(EVT_CB cb, uint8_t *id);
int  EVT_PostEvent(uint8_t id, uint32_t arg);
void EVT_DoEvent(void);

/* ---- system ---- */
void SYS_Init(void);
void SYS_Reboot(void);
void SYS_DelayMs(uint32_t ms);

#endif
