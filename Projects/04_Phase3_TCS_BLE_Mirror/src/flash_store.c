/*
 * flash_store.c -- see flash_store.h for the public API and the Phase 2
 * section of the approved port plan for the full design rationale.
 *
 * Layout (32MB GD25Q256E, conservative partial commit -- not the whole
 * chip):
 *   0x000000            settings sector (4KB, 16 x 256-byte round-robin slots)
 *   0x001000            meter 0 (port 1) history log (1MB)
 *   0x101000            meter 1 (port 2) history log (1MB)
 *   0x201000..0x1FFFFFF reserved / untouched (future phases, e.g. OTA)
 *
 * DeleteAll is logical-only (resets RAM indices to make the log appear
 * empty instantly, per SKILL.md's 6000ms command timeout -- physically
 * erasing a full 1MB region is 256 sector-erases, tens of seconds).
 * Physical reclaim happens lazily, the same way normal wraparound does:
 * flash_store_append_record() erases-ahead exactly the sector it's about
 * to write into, whenever write_index crosses a sector boundary.
 *
 * Boot-time index recovery is a simple forward linear scan from slot 0
 * stopping at the first invalid (magic/CRC mismatch -- i.e. erased or
 * never written) record. This assumes the log has not yet wrapped since
 * last erased; full multi-wrap recovery (distinguishing "never written"
 * from "wrapped past this point") is deferred until the log actually
 * fills in real testing, per the plan's explicit scope call-out -- not
 * silently assumed correct.
 */
#include "flash_store.h"
#include "gd25q.h"
#include "log.h"
#include <string.h>
#include <stddef.h>

#define FLASH_MAGIC 0xA5u
#define GD25Q_JEDEC_MFR_GIGADEVICE 0xC8u

#define FLASH_SETTINGS_BASE      0x00000000u
#define FLASH_SETTINGS_SIZE      GD25Q_SECTOR_SIZE
#define FLASH_SETTINGS_SLOT_SIZE 256u
#define FLASH_SETTINGS_SLOTS     (FLASH_SETTINGS_SIZE / FLASH_SETTINGS_SLOT_SIZE) /* 16 */

#define FLASH_METER_LOG_SIZE   (1024u * 1024u) /* 1MB per meter */
#define FLASH_METER0_LOG_BASE  (FLASH_SETTINGS_BASE + FLASH_SETTINGS_SIZE)     /* 0x001000 */
#define FLASH_METER1_LOG_BASE  (FLASH_METER0_LOG_BASE + FLASH_METER_LOG_SIZE)  /* 0x101000 */

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint32_t generation;
    flash_settings_t settings;
    uint16_t crc16;
} stored_settings_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  meter;
    uint32_t seq;
    uint32_t timestamp;
    uint32_t delivered_tenths;
    uint32_t final_a_tenths;
    uint32_t final_b_tenths;
    uint32_t initial_a_tenths;
    uint32_t initial_b_tenths;
    uint16_t crc16;
} stored_record_t;

typedef struct {
    uint32_t base;
    uint32_t capacity;          /* total record slots in this meter's region */
    uint32_t records_per_sector;
    uint32_t write_index;
    uint32_t oldest_index;
    uint32_t valid_count;
    uint32_t next_seq;
} log_state_t;

static log_state_t s_log[FLASH_METER_COUNT];
static uint32_t    s_settings_next_slot;
static uint32_t    s_settings_generation;
static int         s_settings_have_valid;

static uint16_t crc16_ccitt(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    uint32_t i, b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (b = 0; b < 8u; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* -------- Settings -------- */

static int settings_read_slot(uint32_t slot, stored_settings_t *out)
{
    gd25q_read(FLASH_SETTINGS_BASE + slot * FLASH_SETTINGS_SLOT_SIZE,
               (uint8_t *)out, sizeof(*out));
    return (out->magic == FLASH_MAGIC) &&
           (out->crc16 == crc16_ccitt(out, offsetof(stored_settings_t, crc16)));
}

static void settings_scan(void)
{
    stored_settings_t rec;
    uint32_t slot;
    uint32_t best_slot = 0;
    uint32_t best_gen = 0;
    int found = 0;

    for (slot = 0; slot < FLASH_SETTINGS_SLOTS; slot++) {
        if (settings_read_slot(slot, &rec) && (!found || rec.generation > best_gen)) {
            found = 1;
            best_gen = rec.generation;
            best_slot = slot;
        }
    }

    s_settings_have_valid = found;
    s_settings_generation = found ? best_gen : 0u;
    s_settings_next_slot = found ? ((best_slot + 1u) % FLASH_SETTINGS_SLOTS) : 0u;
}

int flash_store_load_settings(flash_settings_t *out)
{
    stored_settings_t rec;
    uint32_t slot;
    uint32_t best_slot = 0;
    int found = 0;

    for (slot = 0; slot < FLASH_SETTINGS_SLOTS; slot++) {
        stored_settings_t candidate;
        if (settings_read_slot(slot, &candidate) &&
            (!found || candidate.generation > rec.generation)) {
            found = 1;
            rec = candidate;
            best_slot = slot;
        }
    }
    (void)best_slot;

    if (!found) {
        return 0;
    }
    memcpy(out, &rec.settings, sizeof(*out));
    return 1;
}

void flash_store_save_settings(const flash_settings_t *in)
{
    stored_settings_t rec;
    uint32_t addr;

    if (s_settings_next_slot == 0u) {
        gd25q_sector_erase(FLASH_SETTINGS_BASE);
    }

    rec.magic = FLASH_MAGIC;
    rec.generation = ++s_settings_generation;
    memcpy(&rec.settings, in, sizeof(rec.settings));
    rec.crc16 = crc16_ccitt(&rec, offsetof(stored_settings_t, crc16));

    addr = FLASH_SETTINGS_BASE + s_settings_next_slot * FLASH_SETTINGS_SLOT_SIZE;
    gd25q_page_program(addr, (const uint8_t *)&rec, sizeof(rec));

    s_settings_next_slot = (s_settings_next_slot + 1u) % FLASH_SETTINGS_SLOTS;
    s_settings_have_valid = 1;
}

/* -------- History log -------- */

static log_state_t *log_for(unsigned meter)
{
    return &s_log[meter < FLASH_METER_COUNT ? meter : 0u];
}

static int record_read(const log_state_t *st, uint32_t idx, stored_record_t *out)
{
    gd25q_read(st->base + idx * (uint32_t)sizeof(stored_record_t), (uint8_t *)out, sizeof(*out));
    return (out->magic == FLASH_MAGIC) &&
           (out->crc16 == crc16_ccitt(out, offsetof(stored_record_t, crc16)));
}

static void log_scan(unsigned meter, uint32_t base)
{
    log_state_t *st = log_for(meter);
    stored_record_t rec;
    uint32_t idx = 0;
    uint32_t last_seq = 0;

    st->base = base;
    st->capacity = FLASH_METER_LOG_SIZE / (uint32_t)sizeof(stored_record_t);
    st->records_per_sector = GD25Q_SECTOR_SIZE / (uint32_t)sizeof(stored_record_t);

    while (idx < st->capacity && record_read(st, idx, &rec)) {
        last_seq = rec.seq;
        idx++;
    }

    st->write_index = idx % st->capacity;
    st->oldest_index = 0;
    st->valid_count = idx;
    st->next_seq = (idx > 0u) ? (last_seq + 1u) : 1u;
}

void flash_store_init(void)
{
    gd25q_init();
    settings_scan();
    log_scan(0u, FLASH_METER0_LOG_BASE);
    log_scan(1u, FLASH_METER1_LOG_BASE);
}

int flash_store_selftest(void)
{
    uint8_t id[3];
    uint8_t pattern[32];
    uint8_t readback[32];
    uint32_t i;
    /* Last sector of meter 1's region -- unused by normal operation
     * until that log fully wraps, safe to scribble on for this test. */
    uint32_t scratch_addr = FLASH_METER1_LOG_BASE + FLASH_METER_LOG_SIZE - GD25Q_SECTOR_SIZE;

    gd25q_read_jedec_id(id);
    log_hex("flash_store: JEDEC ID = ", id, 3u);

    if (id[0] != GD25Q_JEDEC_MFR_GIGADEVICE) {
        log_line("flash_store: SELFTEST FAIL -- manufacturer byte is not GigaDevice (0xC8)"
                  " -- SPI0_FLASH_* pins in board_config.h are likely wrong\r\n");
        return 0;
    }

    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)(0xA5u ^ i);
    }

    gd25q_sector_erase(scratch_addr);
    gd25q_page_program(scratch_addr, pattern, sizeof(pattern));
    gd25q_read(scratch_addr, readback, sizeof(readback));

    for (i = 0; i < sizeof(pattern); i++) {
        if (readback[i] != pattern[i]) {
            log_line("flash_store: SELFTEST FAIL -- erase/write/read round trip mismatch\r\n");
            return 0;
        }
    }

    log_line("flash_store: SELFTEST PASS -- JEDEC ID confirmed GigaDevice,"
              " erase/write/read round trip verified\r\n");
    return 1;
}

void flash_store_append_record(unsigned meter, const flash_history_record_t *rec)
{
    log_state_t *st = log_for(meter);
    stored_record_t sr;
    uint32_t idx = st->write_index;
    uint32_t addr = st->base + idx * (uint32_t)sizeof(stored_record_t);

    if ((idx % st->records_per_sector) == 0u) {
        gd25q_sector_erase(addr);
        if (st->valid_count >= st->capacity) {
            st->oldest_index = (st->oldest_index + st->records_per_sector) % st->capacity;
            st->valid_count -= st->records_per_sector;
        }
    }

    sr.magic = FLASH_MAGIC;
    sr.meter = (uint8_t)meter;
    sr.seq = st->next_seq++;
    sr.timestamp = rec->timestamp;
    sr.delivered_tenths = rec->delivered_tenths;
    sr.final_a_tenths = rec->final_a_tenths;
    sr.final_b_tenths = rec->final_b_tenths;
    sr.initial_a_tenths = rec->initial_a_tenths;
    sr.initial_b_tenths = rec->initial_b_tenths;
    sr.crc16 = crc16_ccitt(&sr, offsetof(stored_record_t, crc16));

    gd25q_page_program(addr, (const uint8_t *)&sr, sizeof(sr));

    st->write_index = (idx + 1u) % st->capacity;
    if (st->valid_count < st->capacity) {
        st->valid_count++;
    }
}

uint32_t flash_store_record_count(unsigned meter)
{
    return log_for(meter)->valid_count;
}

uint32_t flash_store_record_capacity(unsigned meter)
{
    return log_for(meter)->capacity;
}

int flash_store_read_record(unsigned meter, uint32_t cursor, flash_history_record_t *out)
{
    log_state_t *st = log_for(meter);
    stored_record_t sr;
    uint32_t idx;

    if (cursor >= st->valid_count) {
        return 0;
    }
    idx = (st->oldest_index + cursor) % st->capacity;
    if (!record_read(st, idx, &sr)) {
        return 0; /* shouldn't happen given our own accounting, but don't hand back garbage */
    }

    out->seq = sr.seq;
    out->timestamp = sr.timestamp;
    out->delivered_tenths = sr.delivered_tenths;
    out->final_a_tenths = sr.final_a_tenths;
    out->final_b_tenths = sr.final_b_tenths;
    out->initial_a_tenths = sr.initial_a_tenths;
    out->initial_b_tenths = sr.initial_b_tenths;
    return 1;
}

int flash_store_earliest_latest(unsigned meter, uint32_t *earliest_ts, uint32_t *latest_ts)
{
    log_state_t *st = log_for(meter);
    flash_history_record_t rec;

    if (st->valid_count == 0u) {
        return 0;
    }
    flash_store_read_record(meter, 0u, &rec);
    *earliest_ts = rec.timestamp;
    flash_store_read_record(meter, st->valid_count - 1u, &rec);
    *latest_ts = rec.timestamp;
    return 1;
}

void flash_store_erase_all(unsigned meter)
{
    /* Logical-only: makes the log appear empty instantly (matches
     * SKILL.md's 6000ms command timeout -- physically erasing all 256
     * sectors in a 1MB region takes tens of seconds). Old bytes are
     * reclaimed lazily by flash_store_append_record()'s existing
     * erase-ahead-on-sector-boundary logic as new records are written. */
    log_state_t *st = log_for(meter);
    st->write_index = 0;
    st->oldest_index = 0;
    st->valid_count = 0;
    /* next_seq deliberately NOT reset -- sequence numbers stay globally
     * monotonic across a DeleteAll, matching the boot-scan recovery
     * algorithm's assumption that seq only ever increases.
     *
     * KNOWN GAP: this logical erase is RAM-only. If the MCU resets before
     * any new record overwrites the old physical data, flash_store_init()'s
     * boot scan will find those still-valid-CRC old records again and
     * "resurrect" them -- DeleteAll doesn't persist across an immediate
     * reset. Fixing this properly needs a durable tombstone (e.g. a
     * generation counter alongside the settings block), deferred as
     * out of scope for this simple design until it's shown to matter. */
}
