/*
 * app.c - module init sequencing (V2.89 app.c) and reply routing by send mode.
 */
#include "app.h"
#include "drv.h"

static uint8_t s_init_tmr;

static void app_init_timeout(void *arg)
{
    (void)arg;
    LED_Play(LED_PWR, LED_OFF);
    LED_Play(LED_GPS, LED_OFF);
    LED_Play(LED_WIFI, LED_OFF);
    LED_Play(LED_BT, LED_OFF);

    APPCFG_Init();
    APPEC20_Init();
    APPBT_Init();
    APPLCR_Init();

    TMR_Kill(s_init_tmr);
    DBG(DBG_I, "---APP Init Ok!---");
}

void APP_Init(void)
{
    DBG(DBG_I, "---APP Init Start!---");

    /* all four status LEDs fast-blink until the modules come up (V2.89) */
    LED_Play(LED_PWR, LED_BLINK_100);
    LED_Play(LED_WIFI, LED_BLINK_100);
    LED_Play(LED_GPS, LED_BLINK_100);
    LED_Play(LED_BT, LED_BLINK_100);

    TMR_Creat(3000U, app_init_timeout, NULL, &s_init_tmr);
    TMR_Start(s_init_tmr);
}

void APP_SendReplyLen(const uint8_t *s, uint16_t n)
{
    switch(APPCFG_GetSendMode()) {
    case E_SEND_WIFI:
        APPEC20_SendDataToWifi(s, n);
        break;
    case E_SEND_4G:
        APPEC20_SendDataToServer(s, n);
        break;
    case E_SEND_BT:
    default:
        APPBT_SendDataToBt(s, n);
        break;
    }
}

void APP_SendReply(const char *s)
{
    uint16_t n = 0;
    while(s[n]) {
        n++;
    }
    APP_SendReplyLen((const uint8_t *)s, n);
}
