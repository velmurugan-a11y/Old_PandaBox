/*
 * app_ec20.c - Quectel EC25 4G + GNSS + WiFi-AP, UART4 115200, 20 ms pack interval.
 * AT init table and behaviour from X-Box V2.89 app_ec20.c (see spec/v289_re_notes_2.md section 12).
 *
 * This module drives the modem exactly like Leo: power sequence PE2 then PWRKEY 600 ms, then a
 * 15-command AT init table, then heartbeat / CSQ / GPS at runtime, and TCP to the server.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "app.h"
#include "drv.h"
#include "util.h"

/* GPS snapshot, consumed by app_lcr and the heartbeat */
typedef struct {
    uint32_t lon;   /* ddmm.mmmm * 2, |1 if 'W' */
    uint32_t lonmm;
    uint32_t lat;   /* * 2, |1 if 'N' */
    uint32_t latmm;
    uint32_t time;
} T_GPS;
static T_GPS s_gps;

static const char *const s_at[15] = {
    "ATE0", "AT+QGMR", "AT+CGSN", "AT+CIMI", NULL /*QWSSID*/, NULL /*QWAUTH*/, "AT+QWIFI=1",
    "AT+QWTOCLIEN=1,5553", "AT+CSQ", "AT+QIACT=1", NULL /*QICSGP*/, NULL /*QIOPEN*/,
    "AT+QGPS=1", "AT+QGPSCFG=\"nmeasrc\",1", "AT+QDATAFWDHEX=1"
};

static uint8_t s_idx;          /* init index 0..15 (0x20000074) */
static uint8_t s_have_sim;     /* 0x20000085 */
static uint8_t s_tcp;          /* 0=idle 1=opening 2=connected 4=closed */
static uint8_t s_busy;         /* UART4 send busy 0x20000087 */
static uint8_t s_wifi_client;  /* 0x2000007B */
static char s_imei[16];        /* 0x20002670 */
static uint8_t s_evt_send, s_evt_gps, s_tmr_cmd, s_tmr_heart, s_tmr_csq;

const char *APPEC20_GetImei(void)
{
    return s_imei;
}

static void modem_send_line(const char *fmt, ...)
{
    char b[80];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if(n > 0) {
        s_busy = 1;
        HAL_UartSend(E_HAL_UART_EC20, b, (uint16_t)n);
    }
}

/* send one line to the TCP server: append ,<IMEI>\r\n unless it starts with "wr" (V2.89 0x0800923C) */
void APPEC20_SendDataToServer(const uint8_t *buf, uint16_t len)
{
    static char body[600];
    char at[24];
    uint16_t m = 0;

    if(len > 500U) {
        len = 500U;
    }
    memcpy(body, buf, len);
    if(!(len >= 2U && body[0] == 'w' && body[1] == 'r')) {
        if(len && body[len - 1] != ',' && body[len - 1] != ';') {
            body[m = len] = ',';
            m++;
        } else {
            m = len;
        }
        memcpy(&body[m], s_imei, strlen(s_imei));
        m += (uint16_t)strlen(s_imei);
        body[m++] = '\r';
        body[m++] = '\n';
    } else {
        m = len;
    }
    snprintf(at, sizeof(at), "AT+QISEND=0,%u\r\n", m);
    s_busy = 1;
    HAL_UartSend(E_HAL_UART_EC20, at, (uint16_t)strlen(at));
    HAL_DelayMs(5);
    HAL_UartSend(E_HAL_UART_EC20, body, m);
}

/* WiFi forward, hex encoded (V2.89 0x0800934C) */
void APPEC20_SendDataToWifi(const uint8_t *buf, uint16_t len)
{
    static char at[600];
    static char hex[1100];
    uint16_t n;

    if(!s_wifi_client || len > 500U) {
        return;
    }
    n = (uint16_t)UTIL_DataToHexString(hex, buf, len);
    snprintf(at, sizeof(at), "AT+QDATAFWD=0,3,%u,\"%s\",1\r\n", (unsigned)(2 * len), hex);
    (void)n;
    s_busy = 1;
    HAL_UartSend(E_HAL_UART_EC20, at, (uint16_t)strlen(at));
}

/* build "AT+..." for init index idx into the modem */
static void send_init_cmd(uint32_t arg)
{
    T_BOX_PARAM cfg;
    (void)arg;

    APPCFG_GetBoxParam(&cfg);
    if(s_idx >= 15U) {
        return;
    }
    /* skip QIOPEN(11)/QIACT(9) when no SIM */
    if(!s_have_sim && (s_idx == 9U || s_idx == 11U)) {
        DBG(DBG_D, "No sim, jump this cmd %s", s_idx == 11U ? "QIOPEN!" : "QIACT!");
        s_idx++;
        EVT_PostEvent(s_evt_send, 0);
        return;
    }
    switch(s_idx) {
    case 4:
        if(cfg.wifiSsidFlag == 0x5AU) {
            modem_send_line("AT+QWSSID=%s\r\n", cfg.wifiSsid);
        } else {
            modem_send_line("AT+QWSSID=TBOX_APP\r\n");
        }
        break;
    case 5:
        if(cfg.wifiPwdFlag == 0x5AU) {
            modem_send_line("AT+QWAUTH=5,4,\"%s\"\r\n", cfg.wifiPwd);
        } else {
            modem_send_line("AT+QWAUTH=5,4,\"123456789\"\r\n");
        }
        break;
    case 10:
        modem_send_line("AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r\n",
                        cfg.serverIp[0] ? cfg.apn : "MOBILE");
        break;
    case 11:
        if(UTIL_IsValidIp(cfg.serverIp)) {
            modem_send_line("AT+QIOPEN=1,0,\"TCP\",\"%s\",%s,0,0\r\n", cfg.serverIp, cfg.serverPort);
        } else {
            modem_send_line("AT+QIOPEN=1,0,\"TCP\",\"118.89.111.211\",9090,0,0\r\n");
        }
        s_tcp = 1;
        break;
    default:
        modem_send_line("%s\r\n", s_at[s_idx]);
        break;
    }
    DBG(DBG_I, "Send to EC20, %s---%d", s_at[s_idx] ? s_at[s_idx] : "(cfg)", (int)s_idx);
    TMR_Restart(s_tmr_cmd);
}

static void cmd_timeout(void *a)
{
    (void)a;
    if(s_idx < 15U) {
        EVT_PostEvent(s_evt_send, 0);
    }
}

static void heart_timeout(void *a)
{
    static uint8_t toggle;
    T_GPS *g = &s_gps;
    (void)a;

    if(s_tcp != 2U || s_busy) {
        return;
    }
    if(toggle ^= 1U) {
        DBG(DBG_I, "---check server status---");
        modem_send_line("AT+QISTATE?\r\n");
    } else {
        char h[160];
        snprintf(h, sizeof(h), "Heart,%u,%u.%u,%u.%u,%u.%u,0.0,0.0,0.0",
                 (unsigned)RTC_GetSec(), (unsigned)(g->lat / 2), 0, (unsigned)(g->lon / 2), 0, 0, 0);
        APPEC20_SendDataToServer((const uint8_t *)h, (uint16_t)strlen(h));
    }
}

static void csq_timeout(void *a)
{
    (void)a;
    if(s_busy) {
        return;
    }
    if(s_tcp == 4U) {
        modem_send_line("AT+QICLOSE=0\r\n");
        modem_send_line("AT+QIOPEN=1,0,\"TCP\",\"118.89.111.211\",9090,0,0\r\n");
        s_tcp = 1;
        DBG(DBG_I, "reconect server");
    } else {
        modem_send_line("AT+CSQ\r\n");
    }
}

static void gps_request(uint32_t arg)
{
    (void)arg;
    if(!s_busy) {
        modem_send_line("AT+QGPSGNMEA=\"RMC\"\r\n");
    } else {
        EVT_PostEvent(s_evt_gps, 0);
    }
}

/* very small NMEA RMC latitude/longitude capture (enough for BoxStatus) */
static void parse_rmc(const char *s)
{
    /* $GPRMC,time,A,lat,N,lon,W,... - store ddmm integer part, hemisphere in LSB */
    const char *p = s;
    int field = 0;
    char lat[16] = {0}, ns = 'S', lon[16] = {0}, ew = 'E';
    while(*p) {
        if(*p == ',') {
            field++;
            const char *q = p + 1;
            if(field == 3) { int i = 0; while(q[i] && q[i] != ',' && i < 15) { lat[i] = q[i]; i++; } }
            else if(field == 4) { ns = q[0]; }
            else if(field == 5) { int i = 0; while(q[i] && q[i] != ',' && i < 15) { lon[i] = q[i]; i++; } }
            else if(field == 6) { ew = q[0]; }
        }
        p++;
    }
    s_gps.lat = (UTIL_Str2U32(lat) * 2U) | (ns == 'N' ? 1U : 0U);
    s_gps.lon = (UTIL_Str2U32(lon) * 2U) | (ew == 'W' ? 1U : 0U);
    s_gps.time = RTC_GetSec();
    LED_Flash(LED_GPS, 3);
}

static void ec20_rx_cb(uint8_t *d, uint16_t n)
{
    char *s = (char *)d;

    if(n < 2U) {
        return;
    }
    if(strstr(s, "RDY")) {
        s_idx = 0;
        TMR_Restart(s_tmr_cmd);
        EVT_PostEvent(s_evt_send, 0);
        return;
    }
    /* AT+CGSN: EC25 answers "\r\n<15 digits>\r\n\r\nOK" (bare, no "+CGSN:" prefix or quotes) */
    if(s_idx == 2U || strstr(s, "+CGSN")) {
        const char *q = s;
        while(*q) {
            int i = 0;
            while(q[i] >= '0' && q[i] <= '9') {
                i++;
            }
            if(i == 15) {
                memcpy(s_imei, q, 15U);
                s_imei[15] = 0;
                DBG(DBG_I, "IMEI:%s", s_imei);
                break;
            }
            q += i ? i : 1;
        }
    }
    if(strstr(s, "GPRMC")) {
        parse_rmc(s);
    }
    if(strstr(s, "+QIOPEN")) {
        char *q = strstr(s, "N:");
        if(q && q[5] == '0') {
            s_tcp = 2;
            DBG(DBG_I, "tcp is connect!");
        }
    }
    if(strstr(s, "+QIURC")) {
        if(strstr(s, "recv")) {
            LED_Flash(LED_PWR, 3);
            modem_send_line("AT+QIRD=0,256\r\n");
        } else if(strstr(s, "close")) {
            s_tcp = 4;
            DBG(DBG_I, "TCP is close!");
        }
    }
    /* init-phase progression: an "OK" advances the index */
    if(s_idx < 15U && strstr(s, "OK")) {
        if(s_idx == 3U) {         /* CIMI OK -> we have a SIM */
            s_have_sim = 1;
            TMR_Start(s_tmr_csq);
            LED_Play(LED_PWR, LED_BLINK_900);
        }
        s_idx++;
        s_busy = 0;
        HAL_DelayMs(1);
        EVT_PostEvent(s_evt_send, 0);
        if(s_idx >= 15U) {
            DBG(DBG_D, "---EC25 Init End !---");
            HAL_UartSetPackInterval(E_HAL_UART_EC20, 20U);
            TMR_Stop(s_tmr_cmd);
            if(s_tcp == 2U) {
                char w[40];
                snprintf(w, sizeof(w), "write(IMEI,%s)", s_imei);
                APPEC20_SendDataToServer((const uint8_t *)w, (uint16_t)strlen(w));
                LED_Play(LED_PWR, LED_ON);
                TMR_Start(s_tmr_heart);
            }
        }
        return;
    }
    if(strstr(s, "ERROR") || strstr(s, "+CME ERROR")) {
        s_have_sim = 0;
        s_busy = 0;
    }
    s_busy = 0;
}

static void ec20_txdone(void)
{
    s_busy = 0;
}

void APPEC20_Init(void)
{
    T_BOX_PARAM cfg;

    APPCFG_GetBoxParam(&cfg);
    memset(&s_gps, 0, sizeof(s_gps));
    s_imei[0] = 0;
    s_idx = 0;
    s_tcp = 0;
    s_have_sim = 0;

    HAL_GpioSet(E_HAL_GPIO_WIFI_EN);
    HAL_GpioSet(E_HAL_GPIO_GSM_PWR);
    HAL_DelayMs(100U);
    HAL_GpioSet(E_HAL_GPIO_GSM_PWRKEY);
    HAL_DelayMs(600U);
    HAL_GpioReset(E_HAL_GPIO_GSM_PWRKEY);

    HAL_UartConfig(E_HAL_UART_EC20, 115200U, 0, 0);
    HAL_UartSetPackInterval(E_HAL_UART_EC20, 20U);
    HAL_UartSetCallback(E_HAL_UART_EC20, ec20_rx_cb, ec20_txdone);

    EVT_Creat(send_init_cmd, &s_evt_send);
    EVT_Creat(gps_request, &s_evt_gps);
    TMR_Creat(10000U, cmd_timeout, NULL, &s_tmr_cmd);
    TMR_CreatRepeatTimer(cfg.heatTime ? cfg.heatTime : 1000U, heart_timeout, NULL, &s_tmr_heart);
    TMR_CreatRepeatTimer(15000U, csq_timeout, NULL, &s_tmr_csq);

    LED_Play(LED_PWR, LED_BLINK_100);
    LED_Play(LED_BT, LED_ON);
    DBG(DBG_D, "EC20_Init OK!");

    TMR_Start(s_tmr_cmd);
    EVT_PostEvent(s_evt_send, 0);
}

int APPEC20_SetWifiSsid(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    if(len > 15U) { return 3; }
    APPCFG_GetBoxParam(&c);
    memset(c.wifiSsid, 0, sizeof(c.wifiSsid));
    memcpy(c.wifiSsid, s, len);
    c.wifiSsidFlag = 0x5AU;
    APPCFG_SetBoxParam(&c);
    return 0;
}

int APPEC20_SetWifiPassword(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    if(len < 8U || len > 15U) { return 3; }
    APPCFG_GetBoxParam(&c);
    memset(c.wifiPwd, 0, sizeof(c.wifiPwd));
    memcpy(c.wifiPwd, s, len);
    c.wifiPwdFlag = 0x5AU;
    APPCFG_SetBoxParam(&c);
    return 0;
}

int APPEC20_SetServerIp(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    if(len < 10U || len > 20U) { return 4; }
    APPCFG_GetBoxParam(&c);
    memset(c.serverIp, 0, sizeof(c.serverIp));
    memcpy(c.serverIp, s, len > 15U ? 15U : len);
    APPCFG_SetBoxParam(&c);
    return 0;
}

int APPEC20_SetServerPort(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    if(len < 2U || len > 6U) { return 4; }
    APPCFG_GetBoxParam(&c);
    memset(c.serverPort, 0, sizeof(c.serverPort));
    memcpy(c.serverPort, s, len > 7U ? 7U : len);
    APPCFG_SetBoxParam(&c);
    return 0;
}

int APPEC20_Set4GApn(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    if(len > 15U) { return 3; }
    APPCFG_GetBoxParam(&c);
    memset(c.apn, 0, sizeof(c.apn));
    memcpy(c.apn, s, len);
    APPCFG_SetBoxParam(&c);
    return 0;
}

int APPEC20_SetHeatTime(const char *s, uint16_t len)
{
    T_BOX_PARAM c;
    uint32_t v = UTIL_Str2U32(s);
    if(len > 5U) { return 3; }
    if(v <= 100U || v >= 90000U) { return 5; }
    APPCFG_GetBoxParam(&c);
    c.heatTime = v;
    c.heatTimeFlag = 0x5AU;
    APPCFG_SetBoxParam(&c);
    TMR_SetPeriod(s_tmr_heart, v);
    TMR_Restart(s_tmr_heart);
    return 0;
}
