#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdint.h>

/*
 * flash_store.c -- simple (not wear-leveled) persistent storage on the
 * GD25Q256E: a round-robin settings block, and one circular fixed-record
 * history log per meter. See the Phase 2 section of the approved port
 * plan for the full design rationale.
 *
 * NOT YET WIRED into cmd.c -- board_config.h's SPI0_FLASH_* pin
 * assignment is unconfirmed (no schematic data exists for this chip on
 * this board). flash_store_selftest() is the gate: only after it passes
 * on real hardware (confirming both the pin guess via JEDEC ID and a
 * real erase/write/read round trip) should cmd.c start calling the
 * settings/history functions below for real persistence.
 */

/* Brings up gd25q.c and recovers this session's RAM index state (settings
 * generation/slot, per-meter log write/oldest/valid indices) by scanning
 * flash. Call once at boot, before flash_store_selftest() and before any
 * other flash_store_* call. */
void flash_store_init(void);

/* JEDEC ID read (logged) + full erase/write/read/verify round trip on a
 * scratch sector at the end of meter 2's log region. Returns 1 on pass,
 * 0 on fail -- a fail means either the pin guess is wrong (JEDEC ID
 * won't be GigaDevice's 0xC8 mfr byte) or a real wiring/timing problem.
 * Logs details via log_line() either way. Safe to call repeatedly. */
int flash_store_selftest(void);

/* -------- Settings (round-robin key-value block) -------- */

#define FLASH_APN_MAX  32u
#define FLASH_IP_MAX   32u
#define FLASH_NODES_PER_PORT 10u

typedef struct __attribute__((packed)) {
    char    bt_name[24];
    char    bt_pwd[8];
    char    wifi_name[24];
    char    wifi_pwd[24];
    char    apn[FLASH_APN_MAX];
    char    server_ip[FLASH_IP_MAX];
    uint16_t server_port;
    uint8_t  rs485;
    uint8_t  mode;
    uint8_t  port1_nodes[FLASH_NODES_PER_PORT];
    uint8_t  port2_nodes[FLASH_NODES_PER_PORT];
} flash_settings_t;

/* Returns 1 and fills *out if a valid saved settings record exists
 * (highest generation with matching magic + CRC16), 0 if the sector has
 * never been written (fresh/erased chip) or nothing validates -- caller
 * should keep its own hardcoded defaults in that case. */
int flash_store_load_settings(flash_settings_t *out);

/* Appends a fresh copy of *in as the next generation in the settings
 * sector (erasing and restarting at slot 0 once the sector's 16 slots
 * are used up). Call after every Set-command or node-mapping change. */
void flash_store_save_settings(const flash_settings_t *in);

/* -------- History log (one circular region per meter) -------- */

#define FLASH_METER_COUNT 2u /* meter 0 = port 1, meter 1 = port 2 */

typedef struct {
    uint32_t seq;
    uint32_t timestamp;
    uint32_t delivered_tenths;
    uint32_t final_a_tenths;
    uint32_t final_b_tenths;
    uint32_t initial_a_tenths;
    uint32_t initial_b_tenths;
} flash_history_record_t;

/* `meter` is 0-based (0 or 1), matching cmd.c's meter_index() convention. */
void     flash_store_append_record(unsigned meter, const flash_history_record_t *rec);
uint32_t flash_store_record_count(unsigned meter);    /* currently valid records */
uint32_t flash_store_record_capacity(unsigned meter); /* total slots in the region */

/* cursor 0 = oldest surviving record, cursor (count-1) = newest. Returns
 * 1 and fills *out if cursor < flash_store_record_count(meter), else 0. */
int flash_store_read_record(unsigned meter, uint32_t cursor, flash_history_record_t *out);

/* Earliest/latest timestamp currently stored (for HisDataTime). Returns
 * 0 (both outputs untouched) if the log is empty. */
int flash_store_earliest_latest(unsigned meter, uint32_t *earliest_ts, uint32_t *latest_ts);

/* Erases every sector in this meter's region and resets its indices. */
void flash_store_erase_all(unsigned meter);

#endif /* FLASH_STORE_H */
