/*
 * app_cfg.c - T_BOX_PARAM in the last flash page, same behaviour as V2.89 app_cfg.c.
 * Default server 118.89.111.211:80, WiFi TBOX_APP / 123456789, BT PandaBrain / 1234, HeatTime 1000 ms.
 */
#include <string.h>
#include "app.h"
#include "util.h"

#define CFG_MAGIC_VALID 0x5AU

static T_BOX_PARAM s_cfg;      /* RAM copy, V2.89 keeps it at 0x2000295C */
static uint8_t s_send_mode = E_SEND_BT;

static void strip_first16(char *s)
{
    UTIL_RemoveStrNewLine(s, 16);
}

void APPCFG_GetBoxParam(T_BOX_PARAM *out)
{
    if(s_cfg.wifiSsidFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.wifiSsid);
    }
    if(s_cfg.wifiPwdFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.wifiPwd);
    }
    if(s_cfg.btNameFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.btName);
    }
    memcpy(out, &s_cfg, sizeof(s_cfg));
}

void APPCFG_SetBoxParam(const T_BOX_PARAM *in)
{
    memcpy(&s_cfg, in, sizeof(s_cfg));
    if(s_cfg.wifiSsidFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.wifiSsid);
    }
    if(s_cfg.wifiPwdFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.wifiPwd);
    }
    if(s_cfg.btNameFlag == CFG_MAGIC_VALID) {
        strip_first16(s_cfg.btName);
    }
    HAL_FlashUserDataWrite(0, &s_cfg, sizeof(s_cfg));
}

void APPCFG_ReDefaultBoxParam(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    memcpy(s_cfg.serverIp, "118.89.111.211", 15);
    memcpy(s_cfg.serverPort, "80", 3);
    HAL_FlashUserDataWrite(0, &s_cfg, sizeof(s_cfg));
}

void APPCFG_Init(void)
{
    HAL_FlashUserDataRead(0, &s_cfg, sizeof(s_cfg));

    if(s_cfg.wifiSsidFlag == CFG_MAGIC_VALID) {
        DBG(DBG_I, "WIFI SSID: %s", s_cfg.wifiSsid);
    }
    if(s_cfg.wifiPwdFlag == CFG_MAGIC_VALID) {
        DBG(DBG_I, "WIFI PWD: %s", s_cfg.wifiPwd);
    }
    if(s_cfg.btNameFlag == CFG_MAGIC_VALID) {
        DBG(DBG_I, "BT NAME: %s", s_cfg.btName);
    }
    if(s_cfg.btPwdFlag == CFG_MAGIC_VALID) {
        DBG(DBG_I, "BT PWD: %s", s_cfg.btPwd);
    }
    if(s_cfg.heatTimeFlag == CFG_MAGIC_VALID) {
        DBG(DBG_I, "HeatTime: %dms", (int)s_cfg.heatTime);
    } else {
        s_cfg.heatTime = 1000U;
    }
}

void APPCFG_SetSendMode(uint8_t m)
{
    s_send_mode = m;
}

uint8_t APPCFG_GetSendMode(void)
{
    return s_send_mode;
}
