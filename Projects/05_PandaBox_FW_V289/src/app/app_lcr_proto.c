/*!
    \file    proto.c
    \brief   PandaBox <-> App ASCII protocol (Panada-Box Communication Protocol Rev 1.86)

    Request : "<Cmd>[ a,b,...][,]\r\n"   Reply: "Lx<Cmd> ...\r\n"
    Reply formats follow Leo's FW 2.862/2.891 captures and what the PandaBox tester (v14) validates.
    Meter commands take the meter NODE (Start/Stop/Pause/Print/PresetGross/PresetNet/GetData/...); SwitchState,
    GetLcrNode and ModifyLcrNode take the PORT (1/2), as in the protocol document.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gd32f30x.h"
#include "app.h"
#include "lcr_host.h"
#include "app_compat.h"

#ifndef LCR_RS485_ENABLE
#define LCR_RS485_ENABLE 0          /* keep in step with app_lcr.c */
#endif

/* ------------------------------------------------------------------ box configuration (RAM) */

static struct {
    uint8_t mode;               /* 1 bridge, 2 LCR/cmd, 3 print */
    uint8_t rs485;              /* SetRs485: 1 RS485, 0 RS232 */
    char    bt_name[17];
    char    bt_pwd[5];
    char    wifi_name[17];
    char    wifi_pwd[17];
    char    server_ip[21];
    char    server_port[7];
    char    apn[17];
    uint32_t heat_ms;
    uint8_t app_slot;
} cfg;

static char imei[16] = "000000000000000";
static uint32_t pending_reset_at, pending_rename_at;

/* history streaming state (GetData n,1 / GetDataEcho) */
static int hist_port = -1;

const char *app_imei(void)
{
    return imei;
}

void app_set_imei(const char *s)
{
    if(strlen(s) == 15U) {
        memcpy(imei, s, 16U);
    }
}

const char *app_bt_name(void)
{
    return cfg.bt_name;
}

void proto_init(void)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = 2U;
    cfg.rs485 = 0U;                     /* APPLCR_Init powers the RS232 transceivers (PE5) */
    strcpy(cfg.bt_name, "PandaBrain");
    strcpy(cfg.bt_pwd, "1234");
    strcpy(cfg.wifi_name, "TBOX_APP");
    strcpy(cfg.wifi_pwd, "123456789");
    strcpy(cfg.server_ip, "34.121.179.10");
    strcpy(cfg.server_port, "8080");
    cfg.heat_ms = 1000U;
    cfg.app_slot = 1U;
}

/* ------------------------------------------------------------------ helpers */

static char argbuf[160];
static char *argv_[12];
static int argc_;

/* "Cmd a,b,c," -> name + args (commas and spaces both separate, empty args dropped) */
static void split(const char *line, char *name, uint32_t name_max)
{
    char *p;
    uint32_t i = 0U;

    while(*line == ' ') {
        line++;
    }
    while(*line && *line != ' ' && *line != ',' && i < name_max - 1U) {
        name[i++] = *line++;
    }
    name[i] = '\0';
    strncpy(argbuf, line, sizeof(argbuf) - 1U);
    argbuf[sizeof(argbuf) - 1U] = '\0';
    argc_ = 0;
    p = strtok(argbuf, " ,");
    while(p && argc_ < 12) {
        argv_[argc_++] = p;
        p = strtok(NULL, " ,");
    }
}

static long arg_l(int i, long def)
{
    return (i < argc_) ? strtol(argv_[i], NULL, 10) : def;
}

static const char *arg_s(int i)
{
    return (i < argc_) ? argv_[i] : "";
}

/* decimal "1234.5" -> tenths, without floating point */
static int32_t parse_tenths(const char *s)
{
    int32_t whole = 0, frac = 0, sign = 1;

    if(*s == '-') {
        sign = -1;
        s++;
    }
    while(isdigit((unsigned char)*s)) {
        whole = whole * 10 + (*s++ - '0');
    }
    if(*s == '.' && isdigit((unsigned char)s[1])) {
        frac = s[1] - '0';
    }
    return sign * (whole * 10 + frac);
}

/* tenths -> "1234.5", clamped at 0 (the tester rejects negative volumes/flow) */
static char *fmt_t(char *b, int32_t v)
{
    if(v < 0) {
        v = 0;
    }
    sprintf(b, "%ld.%ld", (long)(v / 10), (long)(v % 10));
    return b;
}

static int ci_eq(const char *a, const char *b)
{
    while(*a && *b) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void copy_str(char *dst, uint32_t max, const char *src)
{
    strncpy(dst, src, max - 1U);
    dst[max - 1U] = '\0';
}

/* "a.b.c.d", each 0..255 */
static int valid_ip(const char *s)
{
    int parts = 0;

    while(parts < 4) {
        int v = 0, digits = 0;
        while(isdigit((unsigned char)*s) && digits < 4) {
            v = v * 10 + (*s++ - '0');
            digits++;
        }
        if(digits == 0 || digits > 3 || v > 255) {
            return 0;
        }
        parts++;
        if(parts < 4 && *s++ != '.') {
            return 0;
        }
    }
    return *s == '\0';
}

/* all digits, value in lo..hi */
static int valid_num(const char *s, long lo, long hi)
{
    const char *p = s;

    if(*p == '\0') {
        return 0;
    }
    while(*p) {
        if(!isdigit((unsigned char)*p++)) {
            return 0;
        }
    }
    return strtol(s, NULL, 10) >= lo && strtol(s, NULL, 10) <= hi;
}

/* SetWifiName / SetWifiPwd / SetApn: present and at most max characters */
static int set_str(char *dst, uint32_t size, uint32_t max)
{
    if(argc_ < 1 || strlen(arg_s(0)) > max) {
        return 0;
    }
    copy_str(dst, size, arg_s(0));
    return 1;
}

/* one GetData / GetDataTs data line from a port cache or a history record */
static void data_line(char *o, const char *tag, uint8_t node, int one, uint8_t ser, uint32_t ts, const int32_t *v)
{
    char a[16], b[16], c[16], d[16], e[16];

    /* <gross>,<flow>,<final total #17>,<net #18>,<initial total #100>,<reserved #101>,lon,E,lat,N */
    if(one >= 0) {
        sprintf(o, "%s %u,%d,%u,%lu,%s,%s,%s,%s,%s,0.0,0.0,S,0.0,E", tag, node, one, ser, (unsigned long)ts,
                fmt_t(a, v[0]), fmt_t(b, v[1]), fmt_t(c, v[2]), fmt_t(d, v[3]), fmt_t(e, v[4]));
    } else {
        sprintf(o, "%s %u,%u,%lu,%s,%s,%s,%s,%s,0.0,0.0,S,0.0,E", tag, node, ser, (unsigned long)ts,
                fmt_t(a, v[0]), fmt_t(b, v[1]), fmt_t(c, v[2]), fmt_t(d, v[3]), fmt_t(e, v[4]));
    }
}

static const char *mcmd_name(mcmd_t c)
{
    switch(c) {
    case MCMD_START: return "Start";
    case MCMD_STOP:  return "Stop";
    case MCMD_PAUSE: return "Pause";
    case MCMD_PRINT: return "Print";
    default:         return "None";
    }
}

/* ------------------------------------------------------------------ meter commands */

/* Start/Stop/Pause/Print/Resume <node>: LCP Issue Command; reply 0 when the meter accepted it */
static void meter_command(const char *name, uint8_t lcp_cmd, mcmd_t kind, reply_fn_t out)
{
    char o[48];
    long node = arg_l(0, 0);
    int port = lcr_port_of_node((uint8_t)node);
    int rc = -1;

    if(port >= 0 && cfg.mode != 1U) {
        int was_busy = lcr_port[port].busy;
        rc = lcr_issue(port, lcp_cmd);
        /* rc 38 is only a success for a Start to a meter that was not busy (queued behind its counter
           test). While a meter is busy it answers rc 38 to everything and executes nothing (golden
           capture: a Stop during the counter test was dropped) -> report 1 so the App retries. */
        if(rc == LCP_RC_QUEUED && (lcp_cmd != 0U || was_busy)) {
            rc = 1;
        }
        lcr_port[port].last_cmd = kind;
        lcr_port[port].last_cmd_rc = (uint8_t)((rc == 0 || rc == LCP_RC_QUEUED) ? 0 : 1);
    }
    /* 0 = the meter accepted it (LCP rc 0); 1 = unknown node, no answer, or refused by the meter
       (e.g. Start with a required ticket pending -> 121). rc 38 = queued: a real LCR answers a Start
       from idle with rc 38 and starts after its ~4 s counter test, so that is a success. */
    sprintf(o, "Lx%s %d", name, (rc == 0 || rc == LCP_RC_QUEUED) ? 0 : 1);
    out(o);
}

/* ------------------------------------------------------------------ dispatcher */

void proto_handle(const char *line, reply_fn_t out)
{
    char name[24], o[200], a[16];
    int port;
    long n;

    if(!strncmp(line, "Lx", 2) || !strncmp(line, "LX", 2)) {
        line += 2;                      /* real box answers "LxSetBtName x" like "SetBtName x" */
    }
    split(line, name, sizeof(name));
    if(name[0] == '\0') {
        return;
    }

    /* ---- status / info ---- */
    if(ci_eq(name, "BoxStatus")) {
        /* mode, work status, node1, node2, lon, E/W, lat, N/S, 4G (0 none), app link (1 BT) */
        sprintf(o, "LxBoxStatus %u,0,%u,%u,0.0,S,0.0,E,0,1", cfg.mode, lcr_port[0].node, lcr_port[1].node);
        out(o);
    } else if(ci_eq(name, "BoxInfo")) {
        sprintf(o, "LxBoxInfo %s,%s,%s,%s,%s,LCR", FW_HW_VER, FW_HW_DATE, FW_SW_VER, app_build_date(), APPEC20_GetImei());
        out(o);
    } else if(ci_eq(name, "BoxTime")) {
        sprintf(o, "LxBoxTime %lu", (unsigned long)app_time());
        out(o);
    } else if(ci_eq(name, "SetBoxTime")) {
        n = arg_l(0, 0);
        if(n > 1577836800L) {
            app_set_time((uint32_t)n);
        }
        out((n > 1577836800L) ? "LxSetBoxTime 0" : "LxSetBoxTime 1");
    } else if(ci_eq(name, "BoxReset")) {
        out("LxBoxReset 0");
        pending_reset_at = millis() + 500U;
    } else if(ci_eq(name, "SetMode")) {
        n = arg_l(0, 0);
        if(n >= 1 && n <= 3) {
            cfg.mode = (uint8_t)n;
        }
        out((n >= 1 && n <= 3) ? "LxSetMode 0" : "LxSetMode 1");
    } else if(ci_eq(name, "SetRs485")) {
        /* 1 = RS485 transceivers (PE6), 0 = RS232 (PE5); anything else is refused */
        /* RS485 disabled for now (see app_lcr.c LCR_RS485_ENABLE): only 0 = RS232 is accepted */
        if(argc_ >= 1 && valid_num(arg_s(0), 0, LCR_RS485_ENABLE ? 1 : 0)) {
            cfg.rs485 = (uint8_t)arg_l(0, 0);
            lcr_set_rs485(cfg.rs485);
            out("LxSetRs485 0");
        } else {
            out("LxSetRs485 1");
        }
    } else if(ci_eq(name, "SetApp1") || ci_eq(name, "SetApp2")) {
        cfg.app_slot = (uint8_t)(name[6] - '0');
        sprintf(o, "LxSetApp%u,1", cfg.app_slot);
        out(o);
    } else if(ci_eq(name, "SetHeatTime")) {
        cfg.heat_ms = (uint32_t)arg_l(0, 1000);
        out("LxSetHeatTime 0");
    } else if(ci_eq(name, "RdDiagnostics")) {
        /* <4G 0/1>,<app link 1 BT / 2 WiFi>,<BLE rssi dBm (not reported by the YC1021: 0)>,<supply mV>
           supply = PC0 ADC * 3300 / 4096 * 11 (12 V input divider) */
        n = arg_l(0, 0);
        if(argc_ == 0 || lcr_port_of_node((uint8_t)n) >= 0) {
            uint32_t mv = (uint32_t)HAL_AdcGetValue(E_HAL_ADC_VCC12) * 3300UL * 11UL / 4096UL;
#if FW_MATCH_289
            sprintf(o, "LxDiagnostics %d,%d,%d,%lu", 0, 1, 0, (unsigned long)mv);
#else
            sprintf(o, "LxRdDiagnostics %d,%d,%d,%lu", 0, 1, 0, (unsigned long)mv);
#endif
        } else {
            sprintf(o, "LxRdDiagnostics %ld,Error", n);
        }
        out(o);
    } else if(ci_eq(name, "SetDbg")) {
        /* no reply by design */

    /* ---- Bluetooth / WiFi / server ---- */
    } else if(ci_eq(name, "RdBtName")) {
        sprintf(o, "LxRdBtName %s", cfg.bt_name);
        out(o);
    } else if(ci_eq(name, "SetBtName")) {
        if(argc_ && strlen(arg_s(0)) <= 16U) {
            copy_str(cfg.bt_name, sizeof(cfg.bt_name), arg_s(0));
            out("LxSetBtName 0");
            pending_rename_at = millis() + 300U;    /* rename after the reply went out */
        } else {
            out("LxSetBtName 1");
        }
    } else if(ci_eq(name, "SetBtPwd")) {
        /* BT pairing PIN: 1..4 characters (tracker: 12345 -> 1) */
        out(set_str(cfg.bt_pwd, sizeof(cfg.bt_pwd), 4U) ? "LxSetBtPwd 0" : "LxSetBtPwd 1");
    } else if(ci_eq(name, "SetWifiName")) {
        out(set_str(cfg.wifi_name, sizeof(cfg.wifi_name), 16U) ? "LxSetWifiName 0" : "LxSetWifiName 1");
    } else if(ci_eq(name, "SetWifiPwd")) {
        out(set_str(cfg.wifi_pwd, sizeof(cfg.wifi_pwd), 16U) ? "LxSetWifiPwd 0" : "LxSetWifiPwd 1");
    } else if(ci_eq(name, "SetServerIp")) {
        if(argc_ >= 1 && valid_ip(arg_s(0))) {
            copy_str(cfg.server_ip, sizeof(cfg.server_ip), arg_s(0));
            out("LxSetServerIp 0");
        } else {
            out("LxSetServerIp 1");
        }
    } else if(ci_eq(name, "SetServerPort")) {
        if(argc_ >= 1 && valid_num(arg_s(0), 1, 65535)) {
            copy_str(cfg.server_port, sizeof(cfg.server_port), arg_s(0));
            out("LxSetServerPort 0");
        } else {
            out("LxSetServerPort 1");
        }
    } else if(ci_eq(name, "SetApn")) {
        out(set_str(cfg.apn, sizeof(cfg.apn), 16U) ? "LxSetApn 0" : "LxSetApn 1");

    /* ---- meter node configuration ---- */
    } else if(ci_eq(name, "SetPortLcrNode")) {
        long n1 = arg_l(0, 0), n2 = arg_l(1, lcr_port[1].node);
        /* 0..255 each; both ports on the same node would make node -> port lookups ambiguous (Leo's
           source rejects it too) */
        if(argc_ >= 1 && valid_num(arg_s(0), 0, 255) && (argc_ < 2 || valid_num(arg_s(1), 0, 255)) &&
           !(n1 != 0 && n1 == n2)) {
            out("LxSetPortLcrNode 0");      /* answer first: the re-sync below can take ~0.25 s per port */
            lcr_port_set_node(0, (uint8_t)n1);
            lcr_port_set_node(1, (uint8_t)n2);
        } else {
            out("LxSetPortLcrNode 1");
        }
    } else if(ci_eq(name, "RdPortLcrNode")) {
        sprintf(o, "LxRdPortLcrNode %u,%u", lcr_port[0].node, lcr_port[1].node);
        out(o);
    } else if(ci_eq(name, "RdRegister")) {
        sprintf(o, "LxRdRegister %u,%u", lcr_port[0].online, lcr_port[1].online);
        out(o);
    } else if(ci_eq(name, "GetLcrNode")) {
        port = (int)arg_l(0, 0) - 1;
        if(port >= 0 && port < LCR_PORTS && arg_l(1, 1) >= 1 && arg_l(2, 250) <= 250 &&
           arg_l(1, 1) <= arg_l(2, 250) && lcr_port[port].node && lcr_port[port].online) {
            /* V2.89: the port's meter is online -> answer its node at once, no scan (golden capture
               19:04:15, "GetLcrNode 2,2,250" -> "LxFindLcrNode 2,1") */
            sprintf(o, "LxFindLcrNode %d,%u", port + 1, lcr_port[port].node);
            out(o);
        } else if(port >= 0 && port < LCR_PORTS && arg_l(1, 1) >= 1 && arg_l(2, 250) <= 250 &&
           arg_l(1, 1) <= arg_l(2, 250)) {
            int found = lcr_find_node(port, (uint8_t)arg_l(1, 1), (uint8_t)arg_l(2, 250));
            if(found) {
                sprintf(o, "LxFindLcrNode %d,%d,", port + 1, found);
            } else {
                sprintf(o, "LxFindLcrNode 0");
            }
            out(o);
        } else {
            out("LxFindLcrNode 0");
        }
    } else if(ci_eq(name, "ModifyLcrNode")) {
        port = (int)arg_l(0, 0) - 1;
        if(port >= 0 && port < LCR_PORTS && lcr_set_address(port, (uint8_t)arg_l(1, 0), (uint8_t)arg_l(2, 0)) == 0) {
            lcr_port_set_node(port, (uint8_t)arg_l(2, 0));
#if FW_MATCH_289
            out("LxModifytLcrNode 0");      /* V2.89's own spelling */
#else
            out("LxModifyLcrNode 0");
#endif
        } else {
            out("LxModifyLcrNode 1");
        }
    } else if(ci_eq(name, "SwitchState")) {
        port = (int)arg_l(0, 0) - 1;
        if(port >= 0 && port < LCR_PORTS && lcr_port[port].online) {
            static const char *sw[] = {"Between", "Run", "Stop", "Print", "Shift Print", "Calibrate", "?", "?"};
            sprintf(o, "LxSwitchState %u,%s", lcr_port[port].node, sw[lcr_port[port].dev_status & 7U]);
        } else {
            sprintf(o, "LxSwitchState %ld,Error", arg_l(0, 0));
        }
        out(o);

    } else if(ci_eq(name, "RdMtrSetting")) {
        /* <port>,<LCP node #102>,<no-flow timer s #25>,<ticket #37 yes/no/skip>,<preset type #27> */
        uint8_t b102[4], b25[4], b37[2], b27[2];
        port = (int)arg_l(0, 0) - 1;
        if(port >= 0 && port < LCR_PORTS && lcr_port[port].node &&
           lcr_get_field(port, 102U, b102, 4) == 2 && lcr_get_field(port, 25U, b25, 4) == 2 &&
           lcr_get_field(port, 37U, b37, 2) == 1 && lcr_get_field(port, 27U, b27, 2) == 1) {
            static const char *tk[] = {"yes", "no", "skip"};
            static const char *pt[] = {"clear", "multiple", "retain"};
            sprintf(o, "LxRdMtrSetting %d,%u,%u,%s,%s", port + 1, b102[1], (unsigned)((b25[0] << 8) | b25[1]),
                    b37[0] < 3U ? tk[b37[0]] : "?", b27[0] < 3U ? pt[b27[0]] : "?");
        } else {
            sprintf(o, "LxRdMtrSetting %ld,Error", arg_l(0, 0));
        }
        out(o);

    /* ---- meter control ---- */
    } else if(ci_eq(name, "Start")) {
        meter_command("Start", 0U, MCMD_START, out);
    } else if(ci_eq(name, "Resume")) {
        /* Resume (TCS wording) only continues a PAUSED delivery; on an idle meter it would otherwise
           start a new delivery (LCP Cmd 0) -> refuse unless the meter reports state STOP (0x10) */
        port = lcr_port_of_node((uint8_t)arg_l(0, 0));
        if(port >= 0 && lcr_port[port].online && (lcr_port[port].dev_status & 0x70U) == 0x10U) {
            meter_command("Resume", 0U, MCMD_START, out);
        } else {
            out("LxResume 1");
        }
    } else if(ci_eq(name, "Pause")) {
        meter_command("Pause", 1U, MCMD_PAUSE, out);
    } else if(ci_eq(name, "Stop")) {
        meter_command("Stop", 2U, MCMD_STOP, out);
    } else if(ci_eq(name, "Print")) {
        meter_command("Print", 6U, MCMD_PRINT, out);
    } else if(ci_eq(name, "PresetGross") || ci_eq(name, "PresetNet")) {
        int is_net = ci_eq(name, "PresetNet");
        int rc = -1;
        /* first argument = meter NODE, like Start/Stop (Leo's source puts it straight into the LCP
           "to" byte, and the tester's sequences use it that way); the protocol text says "port",
           which only agrees while node == port */
        port = lcr_port_of_node((uint8_t)arg_l(0, 0));
        if(port >= 0 && argc_ == 2) {           /* 3 arguments = TCS form (node,productId,qty): not an LCR */
            int32_t t = parse_tenths(arg_s(1));
            rc = is_net ? lcr_set_net_preset(port, t) : lcr_set_preset(port, t);
        }
        sprintf(o, "Lx%s %d", is_net ? "PresetNet" : "PresetGross", (rc == 0) ? 0 : 1);
        out(o);
    } else if(ci_eq(name, "GetLastCmd")) {
        out("LxGetLastCmd 1,0");            /* V2.89 answers this fixed string */
    } else if(ci_eq(name, "GetLastMtrCmd")) {
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(port >= 0) {
#if FW_MATCH_289
            sprintf(o, "LxGetLastMtrCmd %ld,%s %u", n, mcmd_name(lcr_port[port].last_cmd), lcr_port[port].last_cmd_rc);
#else
            sprintf(o, "LxGetLastMtrCmd %ld,%s,%u", n, mcmd_name(lcr_port[port].last_cmd), lcr_port[port].last_cmd_rc);
#endif
        } else {
            sprintf(o, "LxGetLastMtrCmd %ld,None,1", n);    /* meter / node not configured */
        }
        out(o);

    /* ---- data ---- */
    } else if(ci_eq(name, "GetData")) {
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(cfg.mode == 1U) {
            out("LxGetData Mode 1,");
        } else if(port < 0 || !lcr_port[port].online) {
            out("LxGetData Error,");
        } else if(arg_l(1, 0) == 0) {
            lcr_port_t *lp = &lcr_port[port];
            data_line(o, "LxGetData", (uint8_t)n, 1, lp->serial, lp->ts, lp->v);
            out(o);
        } else {
            /* history: oldest stored record first; the app acknowledges with GetDataEcho */
            const hist_rec_t *h = hist_get(port, 0U);
            hist_port = port;
            if(h) {
                data_line(o, "LxGetDataTs", (uint8_t)n, 0, h->serial, h->ts, h->v);
            } else {
                sprintf(o, "LxGetDataTs %ld,", n);
            }
            out(o);
        }
    } else if(ci_eq(name, "GetDataEcho")) {
        /* <serial just received>,<0 ok / 1 fail>,<records wanted> */
        if(hist_port >= 0) {
            const hist_rec_t *h = hist_get(hist_port, 0U);
            uint8_t node = lcr_port[hist_port].node;
            if(!h || arg_l(1, 0) != 0 || h->serial != (uint8_t)arg_l(0, -1)) {
                /* failure flag or wrong sequence number: the upload stops (protocol 1.86 1.4) */
                sprintf(o, "LxGetDataTs %u,", node);
                hist_port = -1;
                out(o);
                return;
            }
            hist_drop_oldest(hist_port);        /* acknowledged -> deleted, send the next one */
            h = hist_get(hist_port, 0U);
            if(h) {
                data_line(o, "LxGetDataTs", node, 0, h->serial, h->ts, h->v);
            } else {
                sprintf(o, "LxGetDataTs %u,", node);
            }
            out(o);
        } else {
            out("LxGetDataTs 0,");
        }
    } else if(ci_eq(name, "GetDataTs")) {
        uint32_t t0 = (uint32_t)strtoul(arg_s(1), NULL, 10), t1 = (uint32_t)strtoul(arg_s(2), NULL, 10);
        uint16_t i, sent = 0U;
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(argc_ < 3 || strtoul(arg_s(2), NULL, 10) >= 0xFFFFFFFFUL) {
            t1 = 0xFFFFFFFFU;
        }
        if(port >= 0) {
            for(i = 0U; i < hist_count(port) && sent < 100U; i++) {
                const hist_rec_t *h = hist_get(port, i);
                if(h->ts >= t0 && h->ts <= t1) {
                    data_line(o, "LxGetDataTs", (uint8_t)n, -1, (uint8_t)(sent + 1U), h->ts, h->v);
                    out(o);
                    sent++;
                }
            }
        }
        sprintf(o, "LxGetDataTs %ld,", n);
        out(o);
    } else if(ci_eq(name, "HisDataTime")) {
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(port >= 0 && hist_count(port)) {
            sprintf(o, "LxHisDataTime %ld,%lu,%lu,", n, (unsigned long)hist_get(port, 0U)->ts,
                    (unsigned long)hist_get(port, (uint16_t)(hist_count(port) - 1U))->ts);
        } else {
            sprintf(o, "LxHisDataTime %ld,", n);      /* empty: fewer than 3 fields */
        }
        out(o);
    } else if(ci_eq(name, "BoxStorage")) {
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(n <= 0) {
            out("LxBoxStorage 1");              /* invalid meter number (tracker: BoxStorage 0) */
            return;
        }
        {
            uint16_t cnt = (port >= 0) ? hist_count(port) : 0U;
            sprintf(o, "LxBoxStorage %ld,%u,%lu,", n, cnt, (unsigned long)(HIST_CAP - cnt) * 64UL);
        }
        out(o);
    } else if(ci_eq(name, "DeleteAll")) {
        n = arg_l(0, 0);
        port = lcr_port_of_node((uint8_t)n);
        if(port >= 0) {
            hist_clear(port);
        }
        out((port >= 0) ? "LxDeleteAll 0" : "LxDeleteAll 1");
    } else if(ci_eq(name, "Update")) {
        out("LxUpdate 1");                      /* OTA (APP<n>,<len>,<sum> / Imei) not implemented yet (M10) */
    } else if(ci_eq(name, "DirectDelivery")) {
        out("LxStop 1");                /* TCS command: not an LCR box (real LCR box behaviour) */
    } else {
        (void)a;                        /* unknown command: no reply, like the real box */
    }
}

void proto_tick(void)
{
    if(pending_rename_at && (int32_t)(millis() - pending_rename_at) >= 0) {
        pending_rename_at = 0U;
        bt_set_name(cfg.bt_name);
    }
    if(pending_reset_at && (int32_t)(millis() - pending_reset_at) >= 0) {
        NVIC_SystemReset();
    }
}
