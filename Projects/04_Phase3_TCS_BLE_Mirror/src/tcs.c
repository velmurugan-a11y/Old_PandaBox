/*
 * tcs.c -- TCS meter bus protocol, PORTED FROM REAL SOURCE, not
 * disassembly reconstruction. Origin: the real UART_TO_METER_1 v3.01
 * release (src/TCSP_Msg.c for the TX frame builder + CRC table, and the
 * uart2 ISR's TCS-mode branch in src/uart.c for RX framing) -- a
 * completely different, better-grounded situation than the yc1021.c
 * disassembly work in this same project.
 *
 * Frame format (TX, from TCSP_Msg.c, byte-for-byte):
 *   [0x7E start][dest][src=0x41][flag][cmd][len][data...][crc]
 *   Escaping: any 0x7E or 0x7D byte in dest/src/flag/cmd/len/data gets
 *   prefixed with 0x7D before being sent. CRC is computed over
 *   [start..last data byte] (i.e. everything except the CRC byte
 *   itself), using the standard Dallas/Maxim CRC-8 table below.
 *
 * Frame format (RX, from uart.c's uart2_callback ISR, TCS_Port2_
 * Connected==1 branch): wait for 0x7E, then accumulate bytes AS-IS --
 * the real firmware does NOT de-escape on receive. Once 6 bytes are in
 * (start,dest,src,flag,cmd,len), the length byte (index 5) sets
 * expected_len = 7 + data_len. Frame complete once that many bytes have
 * arrived. Reproduced faithfully, not "fixed" -- see this file's
 * confidence note: if real meter responses never contain a literal
 * 0x7E/0x7D in their payload, there's nothing to fix.
 *
 * Confirmed commands (from Port_Scanner.c):
 *   Scan:   dest=node, flag=0x20, cmd=0xA9, no payload
 *   Status: dest=node, flag=0x40, cmd=0x0C, no payload
 *   Status reply: 16-bit big-endian value at reply bytes [8]/[9]
 *     (= data bytes 2/3, since data starts at reply byte 6).
 *
 * Baud: 19200, confirmed from the real release's FSP config (see
 * board_config.h) -- not guessed.
 */
#include "tcs.h"
#include "uart.h"
#include "log.h"
#include "board_config.h"
#include <stddef.h>

/* Which physical meter port to use -- default RS485_1 (PA2/PA3, DB25-1).
 * Build with -DTCS_USE_RS485_2=1 to try the other port (PD8/PD9, DB25-2)
 * instead, in case the board's silkscreen "Port 1" labeling doesn't
 * match what the schematic net names suggested. Same baud both ports. */
#ifndef TCS_USE_RS485_2
#define TCS_USE_RS485_2 0
#endif

#if TCS_USE_RS485_2
static const uart_port_t s_port = {
    .base    = RS485_2_UART_BASE,
    .tx_port = RS485_2_GPIO_PORT, .tx_pin = RS485_2_TX_PIN,
    .rx_port = RS485_2_GPIO_PORT, .rx_pin = RS485_2_RX_PIN,
};
#else
static const uart_port_t s_port = {
    .base    = RS485_1_UART_BASE,
    .tx_port = RS485_1_GPIO_PORT, .tx_pin = RS485_1_TX_PIN,
    .rx_port = RS485_1_GPIO_PORT, .rx_pin = RS485_1_RX_PIN,
};
#endif

#define TCS_START 0x7Eu
#define TCS_ESC   0x7Du
#define TCS_SRC   0x41u /* fixed host source address, matches TCSP_Msg.c's TCS_Source default */

/* Standard Dallas/Maxim CRC-8 table, copied verbatim from the real
 * TCSP_Msg.c (this is a well-known, public lookup table -- not specific
 * to this codebase, just correctly identified and reused here). */
static const uint8_t crc_array[256] = {
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};

static void append_byte(uint8_t byte, uint8_t *frame, uint32_t *pos, uint8_t *crc)
{
    if (byte == TCS_ESC || byte == TCS_START) {
        frame[(*pos)++] = TCS_ESC;
        if (crc) { *crc = crc_array[*crc ^ TCS_ESC]; }
    }
    frame[(*pos)++] = byte;
    if (crc) { *crc = crc_array[*crc ^ byte]; }
}

static uint32_t build_frame(uint8_t *frame, uint8_t dest, uint8_t flag, uint8_t cmd,
                             const uint8_t *data, uint8_t len)
{
    uint32_t pos = 0;
    uint8_t  crc = 0;

    frame[pos++] = TCS_START;
    crc = crc_array[crc ^ TCS_START];

    append_byte(dest, frame, &pos, &crc);
    append_byte(TCS_SRC, frame, &pos, &crc);
    append_byte(flag, frame, &pos, &crc);
    append_byte(cmd, frame, &pos, &crc);
    append_byte(len, frame, &pos, &crc);
    for (uint8_t i = 0; i < len; i++) {
        append_byte(data[i], frame, &pos, &crc);
    }
    append_byte(crc, frame, &pos, NULL);

    return pos;
}

/* ------------------------------------------------------------------ *
 * RX: polled equivalent of the real ISR's TCS-mode branch. No
 * de-escaping (matches the real firmware -- see file header). Frame
 * complete once `7 + data_len` bytes have arrived (start+dest+src+flag+
 * cmd+len = 6, + data_len data bytes, + 1 crc byte).
 * ------------------------------------------------------------------ */
#define RX_BUF_SIZE 64
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_index;
static uint32_t rx_expected;
static int      rx_started;
static int      rx_ready;

static void rx_reset(void)
{
    rx_started = 0;
    rx_index = 0;
    rx_expected = 0;
}

static void rx_feed(uint8_t b)
{
    if (!rx_started) {
        if (b == TCS_START) {
            rx_started = 1;
            rx_index = 0;
            rx_expected = 0;
            rx_buf[rx_index++] = b;
        }
        return;
    }

    if (rx_index >= RX_BUF_SIZE) {
        rx_reset();
        return;
    }
    rx_buf[rx_index++] = b;

    if (rx_index == 6u) {
        uint8_t data_len = rx_buf[5];
        rx_expected = 7u + (uint32_t)data_len;
    }

    if (rx_expected > 0u && rx_index >= rx_expected) {
        rx_ready = 1;
        return; /* leave rx_started set; caller must consume + call rx_reset() */
    }
}

/* TCS_UART_BAUD defaults to the confirmed-from-real-source 19200
 * (board_config.h's RS485_1_UART_BAUD), but is overridable at build time
 * (-DTCS_UART_BAUD=9600) to empirically test the user's suspicion that
 * this specific physical meter runs at 9600 instead. */
#ifndef TCS_UART_BAUD
#define TCS_UART_BAUD RS485_1_UART_BAUD
#endif

void tcs_init(void)
{
    uart_init(&s_port, TCS_UART_BAUD, BOARD_PCLK1_HZ);
    rx_reset();
    rx_ready = 0;
#if TCS_USE_RS485_2
    log_line("TCS: RS485_2 (PD8/PD9, DB25-2) initialized, baud=");
#else
    log_line("TCS: RS485_1 (PA2/PA3, DB25-1) initialized, baud=");
#endif
    log_uint(TCS_UART_BAUD);
    log_line("\r\n");
}

void tcs_poll(void)
{
    while (uart_data_ready(&s_port)) {
        rx_feed(uart_getc(&s_port));
    }
}

static void busy_wait(volatile uint32_t iterations)
{
    while (iterations--) { }
}

int tcs_scan_node(uint8_t node, uint16_t *value)
{
    uint8_t frame[16];
    uint32_t len;

    /* --- Scan frame: dest=node, flag=0x20, cmd=0xA9, no payload --- */
    rx_reset();
    rx_ready = 0;
    len = build_frame(frame, node, 0x20u, 0xA9u, NULL, 0u);
    uart_write(&s_port, frame, len);
    log_line("TCS: sent scan to node ");
    log_uint(node);
    log_line("\r\n");

    /* Bounded wait for any reply to the scan (not strictly required by
     * the protocol, but matches the real firmware pacing one command at
     * a time rather than firing scan+status back to back blind). */
    for (uint32_t i = 0; i < 2000u && !rx_ready; i++) {
        tcs_poll();
        busy_wait(30000u); /* x15 for the Phase 2 clock switch, was 2000 at 8MHz */
    }

    int got_scan_reply = rx_ready;
    rx_reset();
    rx_ready = 0;

    /* --- Status frame: dest=node, flag=0x40, cmd=0x0C, no payload --- */
    len = build_frame(frame, node, 0x40u, 0x0Cu, NULL, 0u);
    uart_write(&s_port, frame, len);
    log_line("TCS: sent status request to node ");
    log_uint(node);
    log_line("\r\n");

    for (uint32_t i = 0; i < 2000u && !rx_ready; i++) {
        tcs_poll();
        busy_wait(30000u); /* x15 for the Phase 2 clock switch, was 2000 at 8MHz */
    }

    if (!rx_ready) {
        log_line("TCS: no status reply\r\n");
        rx_reset();
        return got_scan_reply ? 0 : -1; /* distinguish "scan replied but status didn't" from "nothing at all" only via log, both return failure */
    }

    log_hex("TCS: status reply raw: ", rx_buf, rx_index);

    if (rx_index < 10u) {
        log_line("TCS: status reply too short to contain a value\r\n");
        rx_reset();
        rx_ready = 0;
        return 0;
    }

    *value = ((uint16_t)rx_buf[8] << 8) | (uint16_t)rx_buf[9];
    rx_reset();
    rx_ready = 0;
    return 1;
}

/* ------------------------------------------------------------------ *
 * Phase 3: pure-integer IEEE754 double <-> tenths codec. Design and
 * exact algorithm verified in tests/verify_tcs_double_codec.py (13999+
 * round-trip cases, known real-world values, Inf/NaN/subnormal handling)
 * BEFORE being transcribed here -- re-run that test if this logic
 * changes. No `double`/`float` C type anywhere in this file, matching
 * this project's established no-floats convention.
 * ------------------------------------------------------------------ */
static uint32_t bitlen_u64(uint64_t v)
{
    uint32_t n = 0;
    while (v) { v >>= 1; n++; }
    return n;
}

int32_t tcs_double_bits_to_tenths(const uint8_t wire[8])
{
    uint64_t bits = 0;
    uint32_t i, sign, exp;
    uint64_t mant, significand, numerator, tenths_mag;
    int32_t shift;
    uint32_t rshift;

    for (i = 0; i < 8u; i++) {
        bits = (bits << 8) | wire[i];
    }

    sign = (uint32_t)(bits >> 63) & 1u;
    exp  = (uint32_t)(bits >> 52) & 0x7FFu;
    mant = bits & 0xFFFFFFFFFFFFFull;

    if (exp == 0u || exp == 0x7FFu) {
        return 0; /* zero/subnormal/Inf/NaN -- not a real meter reading */
    }

    significand = (1ull << 52) | mant; /* 53-bit: 1.mantissa * 2^52 */
    shift = (int32_t)exp - 1075;       /* value*10 = significand*10*2^shift */
    numerator = significand * 10ull;   /* fits: 53 bits * 10 < 2^57 */

    if (shift >= 0) {
        return 0; /* unreachable for any realistic meter value (>= ~4.5e15) */
    }

    rshift = (uint32_t)(-shift);
    if (rshift >= 64u) {
        tenths_mag = 0u;
    } else {
        uint64_t rounding = (rshift > 0u) ? (1ull << (rshift - 1u)) : 0u;
        tenths_mag = (numerator + rounding) >> rshift;
    }

    return sign ? -(int32_t)tenths_mag : (int32_t)tenths_mag;
}

void tcs_tenths_to_double_bits(int32_t tenths, uint8_t wire[8])
{
    uint32_t sign = (tenths < 0) ? 1u : 0u;
    uint64_t mag = sign ? (uint64_t)(-(int64_t)tenths) : (uint64_t)tenths;
    uint64_t bits;
    uint32_t i;

    if (mag == 0u) {
        bits = 0u;
    } else {
        uint32_t mbits = bitlen_u64(mag);
        uint32_t shift_up = 63u - mbits;
        uint64_t quotient = (mag << shift_up) / 10ull; /* floor(value * 2^shift_up) */
        uint32_t q_bits = bitlen_u64(quotient);
        int32_t norm_shift = (int32_t)q_bits - 53;
        uint64_t normalized;
        int32_t exp_field;

        if (norm_shift > 0) {
            uint64_t rounding = 1ull << ((uint32_t)norm_shift - 1u);
            normalized = (quotient + rounding) >> (uint32_t)norm_shift;
            if (normalized >= (1ull << 53)) {
                normalized >>= 1;
                norm_shift++;
            }
        } else if (norm_shift < 0) {
            normalized = quotient << (uint32_t)(-norm_shift);
        } else {
            normalized = quotient;
        }

        exp_field = 1075 + norm_shift - (int32_t)shift_up;
        bits = ((uint64_t)sign << 63) | ((uint64_t)exp_field << 52) | (normalized & 0xFFFFFFFFFFFFFull);
    }

    for (i = 0; i < 8u; i++) {
        wire[i] = (uint8_t)(bits >> (8u * (7u - i)));
    }
}

/* ------------------------------------------------------------------ *
 * Phase 3: Start/Stop/Pause/Resume/Print/GetData -- see tcs.h for what's
 * confirmed vs. inferred from the reference, and the port plan's Phase 3
 * section for exact line references.
 * ------------------------------------------------------------------ */

/* ~50ms at 120MHz, calibrated the same way ec25.c's busy_wait is
 * (9000000u ~= 600ms there) -- matches the reference's vTaskDelay(50ms)
 * pacing between Start's 3 steps. */
#define TCS_STEP_DELAY_ITERS 750000u

/* UNCONFIRMED placeholder -- no Lx command exposes product-ID selection
 * anywhere in this project's documented protocol surface (see
 * .cursor/skills/pandabox/SKILL.md). If a real meter rejects product ID
 * 0, fixing this needs a new config path; out of scope for this phase. */
#define TCS_ACTIVE_PRODUCT 0u

/* Sends one frame and blocks (bounded) waiting for any reply, same
 * pattern as tcs_scan_node()'s two calls. On success, rx_buf/rx_index
 * hold the raw reply -- caller must rx_reset()+rx_ready=0 once done
 * reading it, same convention tcs_scan_node() already follows. */
static int send_and_wait(uint8_t node, uint8_t flag, uint8_t cmd,
                          const uint8_t *data, uint8_t len)
{
    uint8_t frame[24];
    uint32_t flen;

    rx_reset();
    rx_ready = 0;
    flen = build_frame(frame, node, flag, cmd, data, len);
    uart_write(&s_port, frame, flen);

    for (uint32_t i = 0; i < 2000u && !rx_ready; i++) {
        tcs_poll();
        busy_wait(30000u);
    }
    return rx_ready;
}

int tcs_start(uint8_t node, uint32_t gross_preset_tenths)
{
    uint8_t frame[24];
    uint32_t len;

    /* Step 1: clear transaction -- no reply check, matches the reference
     * exactly (TCS_Process_CMD.c:792-814). */
    rx_reset();
    rx_ready = 0;
    len = build_frame(frame, node, 0x20u, 0xA9u, NULL, 0u);
    uart_write(&s_port, frame, len);
    busy_wait(TCS_STEP_DELAY_ITERS);

    /* Step 2: configure delivery (direct vs. preset). */
    if (gross_preset_tenths == 0u) {
        uint8_t payload[2];
        payload[0] = (uint8_t)((TCS_ACTIVE_PRODUCT >> 8) & 0xFFu);
        payload[1] = (uint8_t)(TCS_ACTIVE_PRODUCT & 0xFFu);
        len = build_frame(frame, node, 0x20u, 0x37u, payload, 2u);
    } else {
        uint8_t payload[11];
        payload[0] = 0x03u;
        payload[1] = (uint8_t)((TCS_ACTIVE_PRODUCT >> 8) & 0xFFu);
        payload[2] = (uint8_t)(TCS_ACTIVE_PRODUCT & 0xFFu);
        tcs_tenths_to_double_bits((int32_t)gross_preset_tenths, &payload[3]);
        len = build_frame(frame, node, 0x20u, 0x38u, payload, 11u);
    }
    uart_write(&s_port, frame, len);
    busy_wait(TCS_STEP_DELAY_ITERS);

    /* Step 3: start delivery. */
    {
        uint8_t payload[1];
        payload[0] = (gross_preset_tenths == 0u) ? 0x01u : 0x03u;
        len = build_frame(frame, node, 0x20u, 0x3Cu, payload, 1u);
    }
    uart_write(&s_port, frame, len);
    busy_wait(TCS_STEP_DELAY_ITERS);

    log_line("TCS: Start sequence sent to node ");
    log_uint(node);
    log_line("\r\n");
    return 1; /* fire-and-assume-success, matching the reference's own behavior */
}

static int single_frame_cmd(const char *label, uint8_t node, uint8_t cmd)
{
    uint8_t payload[1] = {0x00u};
    int ok = send_and_wait(node, 0x20u, cmd, payload, 1u);

    log_line("TCS: ");
    log_line(label);
    log_line(ok ? " ACKed by node " : " NO REPLY from node ");
    log_uint(node);
    log_line("\r\n");

    rx_reset();
    rx_ready = 0;
    return ok;
}

int tcs_stop(uint8_t node)   { return single_frame_cmd("Stop",   node, 0x3Du); }
int tcs_pause(uint8_t node)  { return single_frame_cmd("Pause",  node, 0x39u); }
int tcs_resume(uint8_t node) { return single_frame_cmd("Resume", node, 0x3Au); }
int tcs_print(uint8_t node)  { return single_frame_cmd("Print",  node, 0x3Eu); }

int tcs_get_data(uint8_t node, tcs_reading_t *out)
{
    static const uint8_t opcodes[4] = {0x42u, 0x2Bu, 0x1Eu, 0x1Cu};
    int32_t *const fields[4] = {
        &out->flow_tenths, &out->gross_tenths,
        &out->system_gross_tenths, &out->net_totalizer_tenths
    };
    uint32_t i;

    for (i = 0; i < 4u; i++) {
        /* flag=0x40 read request, no payload -- matches the one confirmed
         * flag=0x40 example (Status, tcs_scan_node above), not confirmed
         * for these specific field-read opcodes against a real meter. */
        if (!send_and_wait(node, 0x40u, opcodes[i], NULL, 0u) || rx_index < 16u) {
            log_line("TCS: GetData field poll failed or short reply\r\n");
            rx_reset();
            rx_ready = 0;
            return 0;
        }
        /* Double payload assumed at reply byte offset 8, per the
         * reference's tcs_bytes_to_double(&buf[8]) -- UNCONFIRMED against
         * a real captured frame, see tcs.h's header comment. */
        *fields[i] = tcs_double_bits_to_tenths(&rx_buf[8]);
        rx_reset();
        rx_ready = 0;
    }
    return 1;
}
