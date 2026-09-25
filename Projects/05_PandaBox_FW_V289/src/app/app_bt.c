/*
 * app_bt.c - Yichip YC1021 Bluetooth: 105-record init table upload, name/PIN/MAC/visibility,
 * runtime data framing. Behaviour and frame formats copied from X-Box V2.89 app_bt.c and proven
 * on this board by Projects/03. UART3, 115200, 20 ms pack interval.
 *
 * Frames:
 *   phone -> box:  SPP  02 07 <n> data          BLE  02 08 <n> <hLo hHi> data(n-2)
 *   box -> phone:  SPP  01 05 <n> data          BLE  01 09 <n+2> 2A 00 data (<=125 data bytes)
 *   ack:           02 06 02 <cmd> <status>      status/link: 02 00 / 02 02 / 02 03 / 02 05
 */
#include <string.h>
#include <stdio.h>
#include "app.h"
#include "drv.h"

extern const uint16_t bt_init_table_count;
extern const uint16_t bt_init_table_size;
extern const uint8_t  bt_init_table[];

#define BLE_NOTIFY_HANDLE 0x002AU
#define BT_CHUNK_BLE      125U
#define BT_CHUNK_SPP      127U
#define LINE_MAX          400U

static uint8_t s_online;          /* 0x20000058 */
static uint8_t s_is_ble;          /* 0x2000006E: 0 = SPP link, 1 = BLE link */
static volatile uint8_t s_ack_seen, s_ack_cmd, s_ack_status;

/* runtime line assembler */
static char s_asm[LINE_MAX];
static uint32_t s_asm_n;

/* ---- synchronous helpers used during init (raw UART mode) ---- */

static int wait_hci_complete(uint16_t op, uint32_t timeout_ms)
{
    uint8_t e[16];
    uint32_t n = 0, start = HAL_GetTick();
    int c;

    while((HAL_GetTick() - start) < timeout_ms) {
        c = HAL_UartReadByte(E_HAL_UART_BT);
        if(c < 0) {
            HAL_FeedWatchDog();
            continue;
        }
        if(n == 0 && c != 0x04) {
            continue;
        }
        if(n < sizeof(e)) {
            e[n] = (uint8_t)c;
        }
        n++;
        if(n >= 3U && n == 3U + e[2]) {
            if(e[1] == 0x0EU && n >= 7U && (uint16_t)(e[4] | (e[5] << 8)) == op) {
                return e[6] == 0U;
            }
            n = 0;
        }
    }
    return 0;
}

static int yc_cmd(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t pkt[40], e[16];
    uint32_t attempt, n, t;
    int c;

    pkt[0] = 0x01U;
    pkt[1] = cmd;
    pkt[2] = len;
    memcpy(&pkt[3], data, len);
    for(attempt = 0; attempt < 4U; attempt++) {
        HAL_UartSend(E_HAL_UART_BT, pkt, (uint16_t)(3U + len));
        n = 0;
        t = HAL_GetTick();
        while((HAL_GetTick() - t) < 1000U) {
            c = HAL_UartReadByte(E_HAL_UART_BT);
            if(c < 0) {
                HAL_FeedWatchDog();
                continue;
            }
            if(n == 0 && c != 0x02) {
                continue;
            }
            if(n < sizeof(e)) {
                e[n] = (uint8_t)c;
            }
            n++;
            if(n >= 3U && n == 3U + e[2]) {
                if(e[1] == 0x06U && e[2] >= 2U && e[3] == cmd) {
                    return e[4] == 0U;
                }
                n = 0;
            }
        }
    }
    return 0;
}

static int bt_set_name(const char *name)
{
    uint8_t le[24];
    size_t n = strlen(name);
    int ok = 1;

    if(n > 16U) {
        n = 16U;
    }
    ok &= yc_cmd(0x03U, (const uint8_t *)name, (uint8_t)n);
    if(n > 13U) {
        n = 13U;
    }
    memcpy(le, name, n);
    memcpy(&le[n], "BLE", 3U);
    ok &= yc_cmd(0x04U, le, (uint8_t)(n + 3U));
    return ok;
}

static void bt_bringup(void)
{
    static const uint8_t pair_mode[] = {0x00};
    static const uint8_t pin[] = {'1', '2', '3', '4'};
    static const uint8_t visible[] = {0x07};
    T_BOX_PARAM cfg;
    uint32_t uid = HAL_GetUid0();
    uint32_t off = 0, idx = 0, ok_rec = 0, i, v;
    uint8_t mac[6];
    const char *name;
    uint16_t op;
    int ok;

    APPCFG_GetBoxParam(&cfg);
    name = (cfg.btNameFlag == 0x5AU && cfg.btName[0]) ? cfg.btName : "PandaBrain";

    HAL_UartConfig(E_HAL_UART_BT, 115200U, 0, 0);
    HAL_UartSetRaw(E_HAL_UART_BT, 1);
    HAL_GpioSet(E_HAL_GPIO_BT_EN);
    HAL_GpioReset(E_HAL_GPIO_BT_RST);
    HAL_DelayMs(20U);
    HAL_GpioSet(E_HAL_GPIO_BT_RST);
    HAL_DelayMs(100U);
    HAL_UartRxFlush(E_HAL_UART_BT);

    /* patch + GATT upload: one record per iteration, each acked by an HCI Command Complete */
    while(off < bt_init_table_size && idx < bt_init_table_count) {
        uint32_t n = bt_init_table[off];
        op = (uint16_t)(bt_init_table[off + 2U] | (bt_init_table[off + 3U] << 8));
        HAL_UartSend(E_HAL_UART_BT, &bt_init_table[off + 1U], (uint16_t)n);
        if(wait_hci_complete(op, 500U)) {
            ok_rec++;
        }
        off += 1U + n;
        idx++;
    }
    DBG(DBG_I, "YC1021 init table %lu/%u records acknowledged", (unsigned long)ok_rec, bt_init_table_count);
    HAL_DelayMs(300U);
    HAL_UartRxFlush(E_HAL_UART_BT);

    ok = (ok_rec == bt_init_table_count);
    ok &= bt_set_name(name);
    ok &= yc_cmd(0x0CU, pair_mode, 1U);
    if(cfg.btPwdFlag == 0x5AU) {
        ok &= yc_cmd(0x0DU, (const uint8_t *)cfg.btPwd, 4U);
    } else {
        ok &= yc_cmd(0x0DU, pin, 4U);
    }
    for(i = 0; i < 2U; i++) {
        v = uid + 4U + i;
        mac[0] = (uint8_t)v;
        mac[1] = (uint8_t)(v >> 8);
        mac[2] = (uint8_t)(v >> 16);
        mac[3] = (uint8_t)(v >> 24);
        /* BT classic keeps Leo's 0x25 / 0x11. The BLE address is a RANDOM-STATIC address (top two
         * bits = 11) whose low 14 bits change every boot from the RTC (seconds bits 0-13, ~16k
         * addresses). Windows caches a half-open link to a previous address, so a fresh address each
         * boot lets the tester reconnect cleanly (works around the YC1021/Windows reconnect issue);
         * with only 6 bits, addresses repeated within a day and hit a cached one again. */
        mac[4] = i ? (uint8_t)(RTC_GetSec() >> 6) : 0x11U;
        mac[5] = i ? (uint8_t)(0xC0U | (RTC_GetSec() & 0x3FU)) : 0x25U;
        ok &= yc_cmd(i ? 0x01U : 0x00U, mac, 6U);
    }
    ok &= yc_cmd(0x02U, visible, 1U);

    HAL_UartSetRaw(E_HAL_UART_BT, 0);
    HAL_UartSetPackInterval(E_HAL_UART_BT, 20U);
    DBG(DBG_I, "YC1021 bring-up %s -- advertising as \"%s\"/\"%.13sBLE\"",
        ok ? "complete" : "incomplete", name, name);
}

/* ---- runtime data path (packet callback) ---- */

static void bt_line_ready(const char *line, uint16_t n)
{
    if(n == 0) {
        return;
    }
    APPCFG_SetSendMode(E_SEND_BT);
    APPLCR_ProcessCmd((const uint8_t *)line, n);
}

static void assemble(const uint8_t *d, uint16_t n)
{
    uint16_t i;

    LED_Flash(LED_BT, 3);
    for(i = 0; i < n; i++) {
        char c = (char)d[i];
        if(c == '\n') {
            while(s_asm_n && (s_asm[s_asm_n - 1] == '\r' || s_asm[s_asm_n - 1] == ' ')) {
                s_asm_n--;
            }
            s_asm[s_asm_n] = 0;
            bt_line_ready(s_asm, s_asm_n);
            s_asm_n = 0;
        } else if(s_asm_n < LINE_MAX - 1U) {
            s_asm[s_asm_n++] = c;
        }
    }
}

static void bt_rx_cb(uint8_t *e, uint16_t n)
{
    uint16_t i = 0;

    while(i + 3U <= n) {
        if(e[i] != 0x02U) {          /* only 02-type events carry data/status */
            i++;
            continue;
        }
        uint8_t typ = e[i + 1];
        uint8_t len = e[i + 2];
        if((uint16_t)(i + 3U + len) > n) {
            break;
        }
        switch(typ) {
        case 0x06U:                  /* ack / TX-done / rssi */
            if(len >= 2U) {
                s_ack_cmd = e[i + 3];
                s_ack_status = e[i + 4];
                s_ack_seen = 1;
                if(e[i + 3] == 0x05U || e[i + 3] == 0x09U) {
                    /* TX chunk done -> release history streaming */
                }
            }
            break;
        case 0x07U:
            /* SPP data received -> a classic link is up; guarantee replies are routed out */
            s_online = 1; s_is_ble = 0; HAL_GpioSet(E_HAL_GPIO_LED_BT);
            assemble(&e[i + 3], len);
            break;
        case 0x08U:
            /* BLE data received -> a BLE central is connected; mark online so replies are notified
             * even if the 02 02 connect status event was missed */
            s_online = 1; s_is_ble = 1; HAL_GpioSet(E_HAL_GPIO_LED_BT);
            if(len >= 2U) {
                assemble(&e[i + 5], (uint16_t)(len - 2U));
            }
            break;
        case 0x00U:
            s_online = 1; s_is_ble = 0; HAL_GpioSet(E_HAL_GPIO_LED_BT);
            DBG(DBG_D, "---bt is online---");
            break;
        case 0x02U:
            if(len >= 1U) {
                if((e[i + 3] & 0x30U)) {
                    s_online = 1;
                    s_is_ble = (e[i + 3] & 0x20U) ? 1 : 0;
                    HAL_GpioSet(E_HAL_GPIO_LED_BT);
                    DBG(DBG_D, s_is_ble ? "---ble is online---" : "---bt is online---");
                } else {
                    s_online = 0;
                    HAL_GpioReset(E_HAL_GPIO_LED_BT);
                }
            }
            break;
        case 0x03U:
        case 0x05U:
            s_online = 0;
            HAL_GpioReset(E_HAL_GPIO_LED_BT);
            DBG(DBG_D, "---bt is offline---");
            break;
        default:
            break;
        }
        i += 3U + len;
    }
}

void APPBT_Init(void)
{
    s_online = 0;
    s_asm_n = 0;
    bt_bringup();
    HAL_UartSetCallback(E_HAL_UART_BT, bt_rx_cb, NULL);
    DBG(DBG_I, "BT Program is update......");
}

void APPBT_SendDataToBt(const uint8_t *buf, uint16_t len)
{
    static uint8_t pkt[6 + 128];
    char line[LINE_MAX + 4];
    uint16_t total, off = 0, chunk, hdr, maxc;
    uint8_t cmd, tail;

    if(!s_online || len == 0 || len > LINE_MAX) {
        return;
    }
    memcpy(line, buf, len);
    tail = line[len - 1];
    if(tail != ';') {
        line[len++] = '\r';
        line[len++] = '\n';
    }
    LED_Flash(LED_BT, 3);
    cmd = s_is_ble ? 0x09U : 0x05U;
    maxc = s_is_ble ? BT_CHUNK_BLE : BT_CHUNK_SPP;
    total = len;
    while(off < total) {
        chunk = (uint16_t)(total - off);
        if(chunk > maxc) {
            chunk = maxc;
        }
        pkt[0] = 0x01U;
        pkt[1] = cmd;
        if(s_is_ble) {
            pkt[2] = (uint8_t)(chunk + 2U);
            pkt[3] = (uint8_t)(BLE_NOTIFY_HANDLE & 0xFFU);
            pkt[4] = (uint8_t)(BLE_NOTIFY_HANDLE >> 8);
            hdr = 5U;
        } else {
            pkt[2] = (uint8_t)chunk;
            hdr = 3U;
        }
        memcpy(&pkt[hdr], &line[off], chunk);
        HAL_UartSend(E_HAL_UART_BT, pkt, (uint16_t)(hdr + chunk));
        HAL_UartFlush(E_HAL_UART_BT);
        off += chunk;
    }
}

int APPBT_SetBtName(const char *name, uint16_t len)
{
    T_BOX_PARAM cfg;

    if(len > 16U) {
        return 3;
    }
    APPCFG_GetBoxParam(&cfg);
    memset(cfg.btName, 0, sizeof(cfg.btName));
    memcpy(cfg.btName, name, len);
    if(len && cfg.btName[len - 1] == ',') {
        cfg.btName[len - 1] = 0;
    }
    cfg.btNameFlag = 0x5AU;
    APPCFG_SetBoxParam(&cfg);
    return 0;
}

int APPBT_SetBtPwd(const char *pwd, uint16_t len)
{
    T_BOX_PARAM cfg;

    if(len != 4U) {
        return 3;
    }
    APPCFG_GetBoxParam(&cfg);
    memcpy(cfg.btPwd, pwd, 4U);
    cfg.btPwdFlag = 0x5AU;
    APPCFG_SetBoxParam(&cfg);
    return 0;
}

uint8_t APPBT_IsOnline(void)
{
    return s_online;
}
