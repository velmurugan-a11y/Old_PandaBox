/*
 * cmd.c -- Lx command dispatcher implementation.
 *
 * Response formats below are copied from the real PandaBox v3 reference
 * firmware (reference/pandabox-v3-full/.../src/uart.c) and cross-checked
 * against literal strings recovered from a real, working compiled X-Box
 * binary (X-Box_V2.89_APP1/APP2.bin) -- not invented. Field values (node
 * tables, BT name, mode) are this board's OWN simplified in-memory state,
 * since this project has no flash-backed settings/storage system yet.
 *
 * Not-yet-implemented pieces (no RTC, no GPS, no flash storage, no meter
 * verification) are called out at each handler rather than faked with
 * plausible-looking numbers.
 */
#include "cmd.h"
#include "flash_store.h"
#include "tcs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NODES_PER_PORT 10u

static uint8_t s_port1_nodes[NODES_PER_PORT];
static uint8_t s_port2_nodes[NODES_PER_PORT];
static char    s_bt_name[24];
static uint8_t s_mode;
static char    s_last_cmd[64];
static int     s_reset_pending;
static int     s_debug_enabled;

/* -------- Phase 1 (BLE app-protocol layer) additions --------
 * Everything below is RAM-only, lost on reset -- there is no flash-backed
 * settings store yet (see the port plan's Phase 2). Meter-derived values
 * (GetData/GetDataTs/RdDiagnostics) are well-formed MOCK data, not real
 * meter or modem readings -- there is no LCR/TCS command layer or 4G TCP
 * stack wired up yet either (Phases 3-5). Every mock is built to match
 * the exact field layout of a real hardware example captured in
 * .cursor/skills/pandabox/getdata-layout.md, not guessed. */
#define APN_MAX  32u
#define IP_MAX   32u

static char     s_apn[APN_MAX];
static char     s_server_ip[IP_MAX];
static uint16_t s_server_port;
static uint8_t  s_rs485;
static char     s_bt_pwd[8];
static char     s_wifi_name[24];
static char     s_wifi_pwd[24];
static uint32_t s_box_time;

typedef struct {
    char     last_cmd[12]; /* "Start"/"Pause"/"Stop"/"Resume"/"Print"/"None" */
    int      active;       /* 1 = mock delivery running, 0 = idle */
    uint32_t gross_preset;
    uint32_t net_preset;
} mtr_state_t;
static mtr_state_t s_mtr[2]; /* index 0 = meter 1, index 1 = meter 2 */
static uint32_t    s_mock_record_serial;

/* -------- Phase 2 (flash storage) additions --------
 * Real persistence via flash_store.c, gated behind flash_store_selftest()
 * passing on real hardware (NOT yet confirmed -- see board_config.h's
 * SPI0_FLASH_* note). s_persist_enabled lets cmd_selftest.c exercise
 * every Set* command without each one overwriting the real persisted
 * settings with test values. */
static int s_persist_enabled = 1;
static uint32_t s_hist_read_cursor[2]; /* per-meter GetData(flag=1) read cursor */

void cmd_set_persist_enabled(int enabled)
{
    s_persist_enabled = enabled ? 1 : 0;
}

static void save_settings(void)
{
    flash_settings_t s;

    if (!s_persist_enabled) {
        return;
    }
    memcpy(s.bt_name, s_bt_name, sizeof(s.bt_name));
    memcpy(s.bt_pwd, s_bt_pwd, sizeof(s.bt_pwd));
    memcpy(s.wifi_name, s_wifi_name, sizeof(s.wifi_name));
    memcpy(s.wifi_pwd, s_wifi_pwd, sizeof(s.wifi_pwd));
    memcpy(s.apn, s_apn, sizeof(s.apn));
    memcpy(s.server_ip, s_server_ip, sizeof(s.server_ip));
    s.server_port = s_server_port;
    s.rs485 = s_rs485;
    s.mode = s_mode;
    memcpy(s.port1_nodes, s_port1_nodes, sizeof(s.port1_nodes));
    memcpy(s.port2_nodes, s_port2_nodes, sizeof(s.port2_nodes));
    flash_store_save_settings(&s);
}

int cmd_reset_pending(void)
{
    return s_reset_pending;
}

int cmd_debug_enabled(void)
{
    return s_debug_enabled;
}

void cmd_init(void)
{
    memset(s_port1_nodes, 0, sizeof(s_port1_nodes));
    memset(s_port2_nodes, 0, sizeof(s_port2_nodes));
    /* Must match the base name yc1021.c's bring-up actually provisions
     * (yc1021_set_name("PandaBrain") -> advertises "PandaBrain"/"PandaBrainBLE").
     * This is reported as the default since cmd.c doesn't query the module
     * for its live name -- it just assumes bring-up succeeded. */
    strcpy(s_bt_name, "PandaBrain");
    s_mode = 1u;
    s_last_cmd[0] = '\0';
    s_apn[0] = '\0';
    s_server_ip[0] = '\0';
    s_server_port = 0u;
    s_rs485 = 0u;
    s_bt_pwd[0] = '\0';
    s_wifi_name[0] = '\0';
    s_wifi_pwd[0] = '\0';
    s_box_time = 0u;
    memset(s_mtr, 0, sizeof(s_mtr));
    strcpy(s_mtr[0].last_cmd, "None");
    strcpy(s_mtr[1].last_cmd, "None");
    s_mock_record_serial = 0u;
    /* Default ON for this bring-up build so debug/RTT logs are visible
     * from boot without needing a working BLE link to send "SetDbg 1"
     * first -- real firmware defaults this OFF, but that assumes you can
     * already reach it over BLE to turn it on. */
    s_debug_enabled = 1;
    s_hist_read_cursor[0] = 0u;
    s_hist_read_cursor[1] = 0u;

    /* Phase 2: overwrite the hardcoded defaults above with whatever was
     * last persisted, if anything -- flash_store_init() must already
     * have run (see main.c) before this. On a genuinely fresh/erased
     * chip (or before flash_store_selftest() has ever passed), nothing
     * validates and the hardcoded defaults above stand, then get
     * persisted as generation 1 so the next boot has something to load. */
    {
        flash_settings_t s;
        if (flash_store_load_settings(&s)) {
            memcpy(s_bt_name, s.bt_name, sizeof(s_bt_name));
            memcpy(s_bt_pwd, s.bt_pwd, sizeof(s_bt_pwd));
            memcpy(s_wifi_name, s.wifi_name, sizeof(s_wifi_name));
            memcpy(s_wifi_pwd, s.wifi_pwd, sizeof(s_wifi_pwd));
            memcpy(s_apn, s.apn, sizeof(s_apn));
            memcpy(s_server_ip, s.server_ip, sizeof(s_server_ip));
            s_server_port = s.server_port;
            s_rs485 = s.rs485;
            s_mode = s.mode;
            memcpy(s_port1_nodes, s.port1_nodes, sizeof(s_port1_nodes));
            memcpy(s_port2_nodes, s.port2_nodes, sizeof(s_port2_nodes));
        } else {
            save_settings();
        }
    }
}

/* Strips a trailing run of spaces/commas, matching the real firmware's
 * process_command() cleanup (uart.c ~line 2803) so "BoxInfo," and
 * "BoxInfo" parse identically. */
static uint32_t trim_trailing(char *buf, uint32_t len)
{
    while (len > 0u && (buf[len - 1] == ' ' || buf[len - 1] == ',')) {
        len--;
    }
    buf[len] = '\0';
    return len;
}

static int name_is(const char *text, uint32_t token_len, const char *name)
{
    return (strlen(name) == token_len) && (memcmp(text, name, token_len) == 0);
}

/* 0-based meter index (0 or 1) for a "1"/"2" argument, or -1 if invalid. */
static int meter_index(const char *arg)
{
    int m = atoi(arg);
    return (m == 1 || m == 2) ? (m - 1) : -1;
}

/* The LCP node currently assigned to a meter's port (0 = none configured). */
static uint8_t meter_node(int idx)
{
    return (idx == 0) ? s_port1_nodes[0] : s_port2_nodes[0];
}

/* Real meter readings (tcs_get_data(), Phase 3) are signed int32_t
 * tenths and could in principle come back negative under some fault
 * condition; the %lu-based tenths formatting throughout this file
 * assumes non-negative, so clamp rather than let a negative value wrap
 * into a huge unsigned garbage number in the reply. */
static unsigned long clamp_tenths(int32_t v)
{
    return (v < 0) ? 0ul : (unsigned long)v;
}

uint32_t cmd_process_line(const char *line, uint32_t len, char *out, uint32_t out_cap)
{
    char buf[80];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    len = trim_trailing(buf, len);

    uint32_t token_len = (uint32_t)strcspn(buf, " ,");
    const char *arg = (token_len < len) ? (buf + token_len + 1) : "";

    /* Matches the real firmware: querying GetLastCmd/GetLastMtrCmd must
     * not overwrite the very thing being queried with itself. */
    if (len > 0 && len < sizeof(s_last_cmd) &&
        !name_is(buf, token_len, "GetLastCmd") &&
        !name_is(buf, token_len, "GetLastMtrCmd")) {
        memcpy(s_last_cmd, buf, len + 1u);
    }

    /* -------- BoxInfo -- HWver,HWdate,SWver,SWdate,IMEI,model -------- */
    if (name_is(buf, token_len, "BoxInfo")) {
        /* Placeholder HW/SW identifiers for this from-scratch board --
         * not a real assigned version number. IMEI blank: no 4G modem
         * registered yet. Model "GD32" to distinguish from the real
         * "LCR"/"TCS" production models pandabox-tester expects. */
        return (uint32_t)snprintf(out, out_cap,
            "LxBoxInfo 0.1,260101,0.1,260101,,GD32\r\n");
    }

    /* -------- BoxStatus -------- */
    if (name_is(buf, token_len, "BoxStatus")) {
        /* No RTC wired up yet (see board_config.h) -- timestamp is a
         * literal 0, not a fabricated clock reading. No GPS module on
         * this build either. net_status=1 (no server link), bt_wifi=1
         * (BLE, since that's the only channel this can arrive on). */
        return (uint32_t)snprintf(out, out_cap,
            "LxBoxStatus %u,0,%u,%u,0,No GPS,No GPS,1,1\r\n",
            (unsigned)s_mode, (unsigned)s_port1_nodes[0], (unsigned)s_port2_nodes[0]);
    }

    /* -------- BoxTime -------- */
    if (name_is(buf, token_len, "BoxTime")) {
        return (uint32_t)snprintf(out, out_cap, "LxBoxTime 0,No GPS,No GPS\r\n");
    }

    /* -------- RdBtName / SetBtName -------- */
    if (name_is(buf, token_len, "RdBtName")) {
        return (uint32_t)snprintf(out, out_cap, "LxRdBtName %s\r\n", s_bt_name);
    }
    if (name_is(buf, token_len, "SetBtName")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_bt_name)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetBtName 1\r\n");
        }
        memcpy(s_bt_name, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetBtName 0\r\n");
        /* NOTE: does NOT actually rename the YC1021 module -- see the
         * hard-learned-lockout warning in yc1021.c. This only updates
         * what RdBtName reports back until a real, verified rename
         * sequence is available. */
    }

    /* -------- RdPortLcrNode -- one line per port, 10 node slots -------- */
    if (name_is(buf, token_len, "RdPortLcrNode")) {
        int used = snprintf(out, out_cap, "LxRdPortLcrNode 1");
        for (uint32_t i = 0; i < NODES_PER_PORT; i++) {
            used += snprintf(out + used, out_cap - (uint32_t)used, ",%u", s_port1_nodes[i]);
        }
        used += snprintf(out + used, out_cap - (uint32_t)used, "\r\nLxRdPortLcrNode 2");
        for (uint32_t i = 0; i < NODES_PER_PORT; i++) {
            used += snprintf(out + used, out_cap - (uint32_t)used, ",%u", s_port2_nodes[i]);
        }
        used += snprintf(out + used, out_cap - (uint32_t)used, "\r\n");
        return (uint32_t)used;
    }

    /* -------- RdRegister -------- */
    if (name_is(buf, token_len, "RdRegister")) {
        return (uint32_t)snprintf(out, out_cap, "LxRdRegister %u,%u\r\n",
            (s_port1_nodes[0] != 0u) ? 1u : 0u,
            (s_port2_nodes[0] != 0u) ? 1u : 0u);
    }

    /* -------- SetPortLcrNode <node1> <node2> -------- */
    if (name_is(buf, token_len, "SetPortLcrNode")) {
        unsigned long n1 = 0, n2 = 0;
        /* Wire args arrive comma-separated (pandabox-tester rewrites
         * spaces to commas for multi-arg commands) -- accept either. */
        int matched = sscanf(arg, "%lu,%lu", &n1, &n2);
        if (matched != 2) {
            matched = sscanf(arg, "%lu %lu", &n1, &n2);
        }
        if (matched != 2 || n1 > 254u || n2 > 254u) {
            return (uint32_t)snprintf(out, out_cap, "LxSetPortLcrNode 1\r\n");
        }
        /* No port_scanner/meter-presence verification exists on this
         * board yet -- unlike the real firmware, this always accepts
         * the assignment rather than confirming a meter answered. */
        if (n1 != 0u) { s_port1_nodes[0] = (uint8_t)n1; }
        if (n2 != 0u) { s_port2_nodes[0] = (uint8_t)n2; }
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetPortLcrNode 0\r\n");
    }

    /* -------- GetLastCmd -------- */
    if (name_is(buf, token_len, "GetLastCmd")) {
        return (uint32_t)snprintf(out, out_cap, "LxGetLastCmd %s\r\n",
            (s_last_cmd[0] != '\0') ? s_last_cmd : "NONE");
    }

    /* -------- GetLastMtrCmd {meter} -- mock meter state (see s_mtr) --------
     * Field layout + the trailing comma before \r are both per
     * getdata-layout.md ("only command with trailing , before \r"); flag
     * is 1=idle, 0=active (documented, counter-intuitive on purpose). */
    if (name_is(buf, token_len, "GetLastMtrCmd")) {
        int idx = meter_index(arg);
        if (idx < 0) {
            return (uint32_t)snprintf(out, out_cap, "LxGetLastMtrCmd %s,NONE,1,\r\n", arg);
        }
        return (uint32_t)snprintf(out, out_cap, "LxGetLastMtrCmd %d,%s,%u,\r\n",
            idx + 1, s_mtr[idx].last_cmd, s_mtr[idx].active ? 0u : 1u);
    }

    /* -------- BoxStorage <port> -- real record count + remaining capacity -------- */
    if (name_is(buf, token_len, "BoxStorage")) {
        unsigned port = (unsigned)atoi(arg);
        int idx = meter_index(arg);
        if (idx < 0) {
            return (uint32_t)snprintf(out, out_cap, "LxBoxStorage %u,0,0\r\n", port);
        }
        {
            uint32_t count = flash_store_record_count((unsigned)idx);
            uint32_t capacity = flash_store_record_capacity((unsigned)idx);
            return (uint32_t)snprintf(out, out_cap, "LxBoxStorage %u,%lu,%lu\r\n",
                port, (unsigned long)count, (unsigned long)(capacity - count));
        }
    }

    /* -------- DeleteAll <node> -- real (logical) erase, see flash_store.c -------- */
    if (name_is(buf, token_len, "DeleteAll")) {
        int idx = meter_index(arg);
        if (idx >= 0) {
            flash_store_erase_all((unsigned)idx);
            s_hist_read_cursor[idx] = 0u;
        }
        return (uint32_t)snprintf(out, out_cap, "LxDeleteAll 0\r\n");
    }

    /* -------- BoxReset -- real MCU reset, no meter/BLE hardware touched -------- */
    if (name_is(buf, token_len, "BoxReset")) {
        s_reset_pending = 1;
        return (uint32_t)snprintf(out, out_cap, "LxBoxReset 0\r\n");
        /* caller (yc1021.c) sends this reply, then main.c's loop sees
         * cmd_reset_pending() and performs the actual reset -- reply-
         * before-reset ordering matches the real firmware. */
    }

    /* ================= Meter control (mock -- no real meter yet) =================
     * Start/Pause/Stop/Resume/Print/PresetGross/PresetNet all validate the
     * meter argument and that a node is assigned to that port (via
     * SetPortLcrNode), then just update s_mtr's mock state and ack --
     * exactly the behavior a real meter command would produce from the
     * APP's point of view, without a real LCR/TCS bus underneath yet. */
    /* Start/Resume are NOT the same real bus operation even though they
     * shared one mock handler in Phase 1: Start is tcs_start()'s 3-step
     * clear/configure/start sequence (fire-and-assume-success, matching
     * the reference), Resume is a single acked frame (tcs_resume()) --
     * see the port plan's Phase 3 section. */
    if (name_is(buf, token_len, "Start")) {
        int idx = meter_index(arg);
        int ok;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "LxStart 1\r\n");
        }
        ok = tcs_start(meter_node(idx), s_mtr[idx].gross_preset);
        strcpy(s_mtr[idx].last_cmd, "Start");
        s_mtr[idx].active = ok ? 1 : 0;
        return (uint32_t)snprintf(out, out_cap, "LxStart %d\r\n", ok ? 0 : 1);
    }
    if (name_is(buf, token_len, "Resume")) {
        int idx = meter_index(arg);
        int ok;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "LxResume 1\r\n");
        }
        ok = tcs_resume(meter_node(idx));
        strcpy(s_mtr[idx].last_cmd, "Resume");
        s_mtr[idx].active = ok ? 1 : 0;
        return (uint32_t)snprintf(out, out_cap, "LxResume %d\r\n", ok ? 0 : 1);
    }
    if (name_is(buf, token_len, "Pause")) {
        int idx = meter_index(arg);
        int ok;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "LxPause 1\r\n");
        }
        ok = tcs_pause(meter_node(idx));
        strcpy(s_mtr[idx].last_cmd, "Pause");
        s_mtr[idx].active = 0;
        return (uint32_t)snprintf(out, out_cap, "LxPause %d\r\n", ok ? 0 : 1);
    }
    if (name_is(buf, token_len, "Stop")) {
        int idx = meter_index(arg);
        int ok;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "LxStop 1\r\n");
        }
        ok = tcs_stop(meter_node(idx));
        strcpy(s_mtr[idx].last_cmd, "Stop");
        s_mtr[idx].active = 0;
        return (uint32_t)snprintf(out, out_cap, "LxStop %d\r\n", ok ? 0 : 1);
    }
    if (name_is(buf, token_len, "Print")) {
        int idx = meter_index(arg);
        int ok;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "LxPrint 1\r\n");
        }
        ok = tcs_print(meter_node(idx));
        strcpy(s_mtr[idx].last_cmd, "Print");
        return (uint32_t)snprintf(out, out_cap, "LxPrint %d\r\n", ok ? 0 : 1);
    }
    if (name_is(buf, token_len, "PresetGross") || name_is(buf, token_len, "PresetNet")) {
        int is_gross = name_is(buf, token_len, "PresetGross");
        const char *lx_name = is_gross ? "LxPresetGross" : "LxPresetNet";
        unsigned long port = 0, tenths = 0;
        int matched = sscanf(arg, "%lu,%lu", &port, &tenths);
        if (matched != 2) {
            matched = sscanf(arg, "%lu %lu", &port, &tenths);
        }
        int idx = (matched == 2 && (port == 1 || port == 2)) ? (int)(port - 1) : -1;
        if (idx < 0 || meter_node(idx) == 0u) {
            return (uint32_t)snprintf(out, out_cap, "%s 1\r\n", lx_name);
        }
        if (is_gross) { s_mtr[idx].gross_preset = (uint32_t)tenths; }
        else          { s_mtr[idx].net_preset = (uint32_t)tenths; }
        return (uint32_t)snprintf(out, out_cap, "%s 0\r\n", lx_name);
    }

    /* ================= History / storage =================
     * Field layouts copied verbatim (shape + field count) from real
     * hardware examples in .cursor/skills/pandabox/getdata-layout.md.
     * Live-reading values (flag=0) are still fabricated -- there's no
     * real meter yet (Phase 3) -- but internally consistent (finalA =
     * initialA + delivered), not random, and now persisted for real via
     * flash_store.c (Phase 2) so the history path below has real data to
     * read back instead of a fixed mock. */
    if (name_is(buf, token_len, "GetData")) {
        unsigned long meter = 0, flag = 0;
        int matched = sscanf(arg, "%lu,%lu", &meter, &flag);
        if (matched < 1) {
            matched = sscanf(arg, "%lu %lu", &meter, &flag);
        }
        if (matched < 1 || (meter != 1 && meter != 2)) {
            return (uint32_t)snprintf(out, out_cap, "LxGetData Error\r\n");
        }
        if (flag == 0u) {
            /* Live reading -- LxGetData {meter},1,{id},{ts},{delivered},
             * {rate},{finalA},{finalB},{initialA},{initialB},0.0,S,0.0,E
             *
             * Fixed-point (tenths of a gallon) instead of float: this
             * project deliberately has no floats/FPU anywhere (soft-float
             * build, see the FreeRTOS port plan's key decisions), and
             * nano.specs' snprintf has no working %f without pulling in
             * extra float-formatting code purely for a mock response. */
            flash_history_record_t hrec;
            unsigned idx0 = (unsigned)(meter - 1u);
            uint8_t node = meter_node((int)idx0);

            if (node != 0u) {
                /* Real meter path (Phase 3) -- tcs_get_data() gives us
                 * flow/gross/systemGross/netTotalizer directly; it does
                 * NOT give a separate "initial totalizer" reading, so
                 * initialA/B are back-calculated as final-minus-delivered
                 * (same consistency rule the mock already used, just
                 * derived from real numbers) -- assumes both totalizer
                 * registers moved by the delivered amount, UNCONFIRMED
                 * against a real meter (see tcs.h's header comment). */
                tcs_reading_t r;
                if (!tcs_get_data(node, &r)) {
                    return (uint32_t)snprintf(out, out_cap, "LxGetData Error\r\n");
                }
                {
                    unsigned long delivered10 = clamp_tenths(r.gross_tenths);
                    unsigned long rate10 = clamp_tenths(r.flow_tenths);
                    unsigned long finalA10 = clamp_tenths(r.system_gross_tenths);
                    unsigned long finalB10 = clamp_tenths(r.net_totalizer_tenths);
                    unsigned long initialA10 = clamp_tenths(r.system_gross_tenths - r.gross_tenths);
                    unsigned long initialB10 = clamp_tenths(r.net_totalizer_tenths - r.gross_tenths);
                    unsigned long ts;

                    s_mock_record_serial++; /* record id counter -- shared with the mock path below, no RTC yet either way */
                    ts = (unsigned long)(1700000000u + s_mock_record_serial * 10u);

                    hrec.timestamp = (uint32_t)ts;
                    hrec.delivered_tenths = (uint32_t)delivered10;
                    hrec.final_a_tenths = (uint32_t)finalA10;
                    hrec.final_b_tenths = (uint32_t)finalB10;
                    hrec.initial_a_tenths = (uint32_t)initialA10;
                    hrec.initial_b_tenths = (uint32_t)initialB10;
                    flash_store_append_record(idx0, &hrec);

                    return (uint32_t)snprintf(out, out_cap,
                        "LxGetData %lu,1,%lu,%lu,%lu.%lu,%lu.%lu,%lu.%lu,%lu.%lu,%lu.%lu,%lu.%lu,0.0,S,0.0,E\r\n",
                        meter, (unsigned long)s_mock_record_serial, ts,
                        delivered10 / 10u, delivered10 % 10u,
                        rate10 / 10u, rate10 % 10u,
                        finalA10 / 10u, finalA10 % 10u,
                        finalB10 / 10u, finalB10 % 10u,
                        initialA10 / 10u, initialA10 % 10u,
                        initialB10 / 10u, initialB10 % 10u);
                }
            }

            /* No real node assigned -- keep Phase 1's mock fallback so
             * BLE demoing/pandabox-tester regression runs without a
             * physical meter attached still work (intentional, not a
             * gap -- see the port plan's Phase 3 section). */
            s_mock_record_serial++;
            unsigned long delivered10 = 1000u + s_mock_record_serial * 100u; /* 100.0 + serial*10.0 */
            unsigned long initialA10 = 10000u, initialB10 = 9990u;           /* 1000.0, 999.0 */
            unsigned long finalA10 = initialA10 + delivered10;
            unsigned long finalB10 = initialB10 + delivered10;
            unsigned long ts = (unsigned long)(1700000000u + s_mock_record_serial * 10u);

            hrec.timestamp = (uint32_t)ts;
            hrec.delivered_tenths = (uint32_t)delivered10;
            hrec.final_a_tenths = (uint32_t)finalA10;
            hrec.final_b_tenths = (uint32_t)finalB10;
            hrec.initial_a_tenths = (uint32_t)initialA10;
            hrec.initial_b_tenths = (uint32_t)initialB10;
            flash_store_append_record(idx0, &hrec);

            return (uint32_t)snprintf(out, out_cap,
                "LxGetData %lu,1,%lu,%lu,%lu.%lu,5.0,%lu.%lu,%lu.%lu,%lu.%lu,%lu.%lu,0.0,S,0.0,E\r\n",
                meter, (unsigned long)s_mock_record_serial, ts,
                delivered10 / 10u, delivered10 % 10u,
                finalA10 / 10u, finalA10 % 10u,
                finalB10 / 10u, finalB10 % 10u,
                initialA10 / 10u, initialA10 % 10u,
                initialB10 / 10u, initialB10 % 10u);
        }
        /* History path (flag=1): streams real stored records one per
         * call via a per-meter read cursor, ending in "All Read" once
         * exhausted (then resets for next time). UNVERIFIED against real
         * hardware/pandabox-tester -- the "data1=0 marks a history entry
         * vs. 1 for live" convention here is inferred from
         * getdata-layout.md, not empirically confirmed, since no
         * hardware is available this session to test the real app's
         * reaction to it. */
        {
            unsigned m0 = (unsigned)(meter - 1u);
            uint32_t count = flash_store_record_count(m0);
            flash_history_record_t r;

            if (s_hist_read_cursor[m0] >= count) {
                s_hist_read_cursor[m0] = 0u;
                return (uint32_t)snprintf(out, out_cap, "LxGetData All Read\r\n");
            }
            flash_store_read_record(m0, s_hist_read_cursor[m0], &r);
            s_hist_read_cursor[m0]++;
            return (uint32_t)snprintf(out, out_cap,
                "LxGetData %lu,0,%lu,%lu,%lu.%lu,5.0,%lu.%lu,%lu.%lu,%lu.%lu,%lu.%lu,0.0,S,0.0,E\r\n",
                meter, (unsigned long)r.seq, (unsigned long)r.timestamp,
                (unsigned long)(r.delivered_tenths / 10u), (unsigned long)(r.delivered_tenths % 10u),
                (unsigned long)(r.final_a_tenths / 10u), (unsigned long)(r.final_a_tenths % 10u),
                (unsigned long)(r.final_b_tenths / 10u), (unsigned long)(r.final_b_tenths % 10u),
                (unsigned long)(r.initial_a_tenths / 10u), (unsigned long)(r.initial_a_tenths % 10u),
                (unsigned long)(r.initial_b_tenths / 10u), (unsigned long)(r.initial_b_tenths % 10u));
        }
    }
    if (name_is(buf, token_len, "GetDataTs")) {
        unsigned long meter = 0, from = 0, to = 0;
        int matched = sscanf(arg, "%lu,%lu,%lu", &meter, &from, &to);
        if (matched != 3) {
            matched = sscanf(arg, "%lu %lu %lu", &meter, &from, &to);
        }
        if (matched != 3 || (meter != 1 && meter != 2)) {
            return (uint32_t)snprintf(out, out_cap, "LxGetDataTs Error\r\n");
        }
        /* Deliberately STILL MOCK (two fixed records, unlike GetData's
         * flag=1 path above) -- Phase 2 wires whatever has an unambiguous
         * real<->mock mapping; a timestamp-range multi-record batch reply
         * plus GetDataEcho's "{seq},{ok},{count}" ack does not (real
         * pagination semantics need empirical confirmation against
         * pandabox-tester, which needs hardware this session doesn't
         * have). Wiring this for real is a Phase 2 follow-up once
         * hardware is back, not deferred to Phase 3+. */
        return (uint32_t)snprintf(out, out_cap,
            "LxGetDataTs %lu,0,%lu,0.0,0.0,73767.4,0.0,73767.4,0.0,0.0,S,0.0,E;"
            "%lu,1,%lu,0.0,0.0,73780.2,0.0,73780.2,0.0,0.0,S,0.0,E\r\n",
            meter, from + 30u, meter, from + 60u);
    }
    if (name_is(buf, token_len, "GetDataEcho")) {
        /* Still mock -- see GetDataTs's comment above; this ack's
         * interaction with a real read cursor is unverified. */
        return (uint32_t)snprintf(out, out_cap, "LxGetDataTs All Read\r\n");
    }
    if (name_is(buf, token_len, "HisDataTime")) {
        int idx = meter_index(arg);
        uint32_t earliest, latest;
        if (idx < 0) {
            return (uint32_t)snprintf(out, out_cap, "LxHisDataTime %s,0,0,\r\n", arg);
        }
        if (!flash_store_earliest_latest((unsigned)idx, &earliest, &latest)) {
            return (uint32_t)snprintf(out, out_cap, "LxHisDataTime %d,0,0,\r\n", idx + 1);
        }
        return (uint32_t)snprintf(out, out_cap, "LxHisDataTime %d,%lu,%lu,\r\n",
            idx + 1, (unsigned long)earliest, (unsigned long)latest);
    }

    /* ================= Config (RAM only -- see Phase 2 for persistence) ================= */
    if (name_is(buf, token_len, "SetMode")) {
        unsigned long mode = 0;
        if (sscanf(arg, "%lu", &mode) != 1 || mode > 3u) {
            return (uint32_t)snprintf(out, out_cap, "LxSetMode 1\r\n");
        }
        s_mode = (uint8_t)mode;
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetMode 0\r\n");
    }
    if (name_is(buf, token_len, "SetBtPwd")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_bt_pwd)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetBtPwd 1\r\n");
        }
        memcpy(s_bt_pwd, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetBtPwd 0\r\n");
    }
    if (name_is(buf, token_len, "SetWifiName")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_wifi_name)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetWifiName 1\r\n");
        }
        memcpy(s_wifi_name, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetWifiName 0\r\n");
    }
    if (name_is(buf, token_len, "SetWifiPwd")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_wifi_pwd)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetWifiPwd 1\r\n");
        }
        memcpy(s_wifi_pwd, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetWifiPwd 0\r\n");
    }
    if (name_is(buf, token_len, "SetServerIp")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_server_ip)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetServerIp 1\r\n");
        }
        memcpy(s_server_ip, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetServerIp 0\r\n");
    }
    if (name_is(buf, token_len, "SetServerPort")) {
        unsigned long port = 0;
        if (sscanf(arg, "%lu", &port) != 1 || port > 65535u) {
            return (uint32_t)snprintf(out, out_cap, "LxSetServerPort 1\r\n");
        }
        s_server_port = (uint16_t)port;
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetServerPort 0\r\n");
    }
    if (name_is(buf, token_len, "SetApn")) {
        uint32_t arg_len = (uint32_t)strlen(arg);
        if (arg_len == 0u || arg_len >= sizeof(s_apn)) {
            return (uint32_t)snprintf(out, out_cap, "LxSetApn 1\r\n");
        }
        memcpy(s_apn, arg, arg_len + 1u);
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxSetApn 0\r\n");
    }
    if (name_is(buf, token_len, "SetBoxTime")) {
        unsigned long unix_time = 0;
        if (sscanf(arg, "%lu", &unix_time) != 1) {
            return (uint32_t)snprintf(out, out_cap, "LxSetBoxTime 1\r\n");
        }
        /* No RTC to actually set yet -- stored so BoxTime reflects it. */
        s_box_time = (uint32_t)unix_time;
        return (uint32_t)snprintf(out, out_cap, "LxSetBoxTime 0\r\n");
    }
    /* SetRs485 / SetApp1 / SetApp2 -- no ack per SKILL.md ("--" in the
     * command table), same silent-drop pattern as SetDbg. SetApp1/2 select
     * a dual-flash-slot boot image; not applicable to this single-image
     * build, so just logged as a no-op rather than acted on. */
    if (name_is(buf, token_len, "SetRs485")) {
        s_rs485 = (atoi(arg) != 0) ? 1u : 0u;
        save_settings();
        return 0u;
    }
    if (name_is(buf, token_len, "SetApp1") || name_is(buf, token_len, "SetApp2")) {
        return 0u;
    }

    /* ================= Node / diagnostics ================= */
    if (name_is(buf, token_len, "ModifyLcrNode")) {
        /* NOTE: the real firmware's actual response is misspelled
         * ("LxModifytLcrNode" -- see SKILL.md). We reply with the correct
         * spelling; if a real driver-app round-trip turns out to require
         * the typo for compatibility, switch this one string. */
        unsigned long port = 0, old_node = 0, new_node = 0;
        int matched = sscanf(arg, "%lu,%lu,%lu", &port, &old_node, &new_node);
        if (matched != 3) {
            matched = sscanf(arg, "%lu %lu %lu", &port, &old_node, &new_node);
        }
        int idx = (matched == 3 && (port == 1 || port == 2)) ? (int)(port - 1) : -1;
        if (idx < 0 || new_node > 254u) {
            return (uint32_t)snprintf(out, out_cap, "LxModifyLcrNode 1\r\n");
        }
        uint8_t *nodes = (idx == 0) ? s_port1_nodes : s_port2_nodes;
        nodes[0] = (uint8_t)new_node;
        save_settings();
        return (uint32_t)snprintf(out, out_cap, "LxModifyLcrNode 0\r\n");
    }
    if (name_is(buf, token_len, "GetLcrNode")) {
        /* Mock bus scan: reports back whatever node is already assigned
         * to the requested port (if any), then a completion marker --
         * there is no real bus scan (Port_Scanner equivalent) yet. */
        unsigned long port = 0, start = 0, end = 0;
        int matched = sscanf(arg, "%lu,%lu,%lu", &port, &start, &end);
        if (matched != 3) {
            matched = sscanf(arg, "%lu %lu %lu", &port, &start, &end);
        }
        int idx = (matched == 3 && (port == 1 || port == 2)) ? (int)(port - 1) : -1;
        int used;
        if (idx < 0) {
            return (uint32_t)snprintf(out, out_cap, "LxGetLcrNode %s,0\r\n", arg);
        }
        used = 0;
        if (meter_node(idx) != 0u) {
            used = snprintf(out, out_cap, "LxFindLcrNode %lu,%u\r\n", port, (unsigned)meter_node(idx));
        }
        used += snprintf(out + used, out_cap - (uint32_t)used, "LxGetLcrNode %lu,0\r\n", port);
        return (uint32_t)used;
    }
    if (name_is(buf, token_len, "SwitchState")) {
        int idx = meter_index(arg);
        if (idx < 0) {
            return (uint32_t)snprintf(out, out_cap, "LxSwitchState %s,0\r\n", arg);
        }
        /* 0=Stop, 1=Run -- matches s_mtr[idx].active. */
        return (uint32_t)snprintf(out, out_cap, "LxSwitchState %d,%u\r\n", idx + 1, s_mtr[idx].active ? 1u : 0u);
    }
    if (name_is(buf, token_len, "RdDiagnostics")) {
        /* Field layout per getdata-layout.md: is4GConnected,
         * connectionType(1=BT,2=WiFi),bluetoothRssi,boxVoltageMilliVolts
         * -- no leading meter-echo field (the doc's table doesn't have
         * one, unlike GetData). Mock values: no 4G TCP yet (Phase 5), BT
         * transport (this is arriving over BLE), plausible RSSI/voltage. */
        return (uint32_t)snprintf(out, out_cap, "LxRdDiagnostics 0,1,-50,12000\r\n");
    }

    /* -------- Update (OTA) -- explicitly not implemented yet -------- */
    if (name_is(buf, token_len, "Update")) {
        return (uint32_t)snprintf(out, out_cap, "LxUpdate 1\r\n");
    }

    /* -------- SetDbg -- never acknowledged over this channel, real behavior -------- */
    if (name_is(buf, token_len, "SetDbg")) {
        s_debug_enabled = (atoi(arg) != 0);
        return 0u;
    }

    return 0u; /* unrecognized command: no reply (matches real firmware's silent drop) */
}
