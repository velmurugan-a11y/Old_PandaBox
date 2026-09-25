/*
 * app.h - application layer, matching Leo's src/app modules (V2.89).
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "hal.h"
#include "sys.h"

/* ---------- T_BOX_PARAM: persisted config, 116 bytes, offsets from V2.89 ---------- */
typedef struct __attribute__((packed)) {
    uint8_t  wifiSsidFlag;   /* 0x00  0x5A = user value valid */
    uint8_t  wifiPwdFlag;    /* 0x01 */
    uint8_t  btNameFlag;     /* 0x02 */
    uint8_t  btPwdFlag;      /* 0x03 */
    char     wifiSsid[16];   /* 0x04 */
    char     wifiPwd[16];    /* 0x14 */
    char     btName[16];     /* 0x24 */
    char     btPwd[16];      /* 0x34  (only 4 used) */
    uint8_t  rsv44[3];       /* 0x44  unaccounted in V2.89 */
    char     serverIp[15];   /* 0x47 */
    char     serverPort[8];  /* 0x56 */
    char     apn[16];        /* 0x5E */
    uint8_t  lcrWorkMode;    /* 0x6E */
    uint8_t  heatTimeFlag;   /* 0x6F */
    uint32_t heatTime;       /* 0x70  heartbeat period ms */
} T_BOX_PARAM;

typedef enum { E_SEND_BT = 0, E_SEND_WIFI, E_SEND_4G } E_SEND_MODE;
typedef enum { E_IDLE_MODE = 0, E_BRIDGE_MODE = 1, E_CMD_MODE = 2 } E_LCR_WORK_MODE;

/* app_cfg.c */
void   APPCFG_Init(void);
void   APPCFG_GetBoxParam(T_BOX_PARAM *out);
void   APPCFG_SetBoxParam(const T_BOX_PARAM *in);
void   APPCFG_ReDefaultBoxParam(void);
void   APPCFG_SetSendMode(uint8_t m);
uint8_t APPCFG_GetSendMode(void);

/* app.c */
void APP_Init(void);
void APP_SendReply(const char *s);          /* route an Lx reply by the current send mode */
void APP_SendReplyLen(const uint8_t *s, uint16_t n);

/* app_bt.c */
void APPBT_Init(void);
void APPBT_SendDataToBt(const uint8_t *buf, uint16_t len);
int  APPBT_SetBtName(const char *name, uint16_t len);
int  APPBT_SetBtPwd(const char *pwd, uint16_t len);
uint8_t APPBT_IsOnline(void);

/* app_ec20.c */
void APPEC20_Init(void);
void APPEC20_SendDataToServer(const uint8_t *buf, uint16_t len);
void APPEC20_SendDataToWifi(const uint8_t *buf, uint16_t len);
const char *APPEC20_GetImei(void);
int  APPEC20_SetWifiSsid(const char *s, uint16_t len);
int  APPEC20_SetWifiPassword(const char *s, uint16_t len);
int  APPEC20_SetServerIp(const char *s, uint16_t len);
int  APPEC20_SetServerPort(const char *s, uint16_t len);
int  APPEC20_Set4GApn(const char *s, uint16_t len);
int  APPEC20_SetHeatTime(const char *s, uint16_t len);

/* app_lcr.c */
void APPLCR_Init(void);
void APPLCR_ProcessCmd(const uint8_t *data, uint16_t len);   /* App -> box command parser */

#endif
