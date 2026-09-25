/*
 * cli.c - USART0 command shell, same commands as V2.89 (appinfo/update/reboot/reason/set debug level/
 * debug on|off|time). Plus 'inject <cmd>' to run an App command from the console for offline testing.
 */
#include <string.h>
#include <stdio.h>
#include "hal.h"
#include "sys.h"
#include "app.h"

static char s_line[128];
static uint16_t s_n;

static void print_appinfo(void)
{
    uint32_t info[6];
    HAL_FlashRead(FLASH_APP_INFO_ADDR, info, sizeof(info));
    DBG_PrintRaw("\r\nu32AppFlag:%x\n\ru32AppLen:%d\n\ru32AppChkSum:%d\n\ru32AppFlag2:%x\n\ru32AppLen2:%d\n\ru32AppChkSum2:%d\n\r",
                 (unsigned)info[0], (int)info[1], (int)info[2], (unsigned)info[3], (int)info[4], (int)info[5]);
}

static void cli_exec(char *line)
{
    if(line[0] == 0) {
        return;
    }
    if(!strcmp(line, "reboot")) {
        SYS_Reboot();
    } else if(!strcmp(line, "reason")) {
        DBG_PrintRaw("\r\nReboot reason: %d, %s", (int)HAL_GetResetReason(), HAL_GetResetReasonStr());
    } else if(!strcmp(line, "appinfo")) {
        print_appinfo();
    } else if(!strncmp(line, "set debug level ", 16)) {
        DBG_SetPrintLevel((uint8_t)(line[16] - '0'));
        DBG_PrintRaw("\r\nDBG level: %s", DBG_LevelName(DBG_GetPrintLevel()));
    } else if(!strcmp(line, "debug on")) {
        DBG_SetOn(1);
    } else if(!strcmp(line, "debug off")) {
        DBG_SetOn(0);
    } else if(!strncmp(line, "inject ", 7)) {
        APPLCR_ProcessCmd((const uint8_t *)line + 7, (uint16_t)strlen(line + 7));
    } else {
        DBG_PrintRaw("\r\nInvalid command");
    }
}

static void cli_rx(uint8_t *d, uint16_t n)
{
    uint16_t i;

    for(i = 0; i < n; i++) {
        char c = (char)d[i];
        if(c == '\r' || c == '\n') {
            if(s_n) {
                s_line[s_n] = 0;
                cli_exec(s_line);
                s_n = 0;
            }
        } else if(s_n < sizeof(s_line) - 1U) {
            s_line[s_n++] = c;
        }
    }
}

void CLI_Init(void)
{
    s_n = 0;
    /* the debug UART is shared with DBG; install the CLI RX callback on it */
    HAL_UartSetPackInterval(E_HAL_UART_PRINT, 50U);
    HAL_UartSetCallback(E_HAL_UART_PRINT, cli_rx, NULL);
    DBG_PrintRaw("\r\nDBG>");
}
