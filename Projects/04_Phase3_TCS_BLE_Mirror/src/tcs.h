#ifndef TCS_H
#define TCS_H

#include <stdint.h>

/* TCS meter bus (RS232-configured DB25 Port 1 = RS485_1 pins on this
 * board's MCU side: USART1, PA2 TX / PA3 RX -- see board_config.h).
 * Ported from the real, working UART_TO_METER_1 v3.01 release
 * (Port_Scanner.c / TCSP_Msg.c / uart.c's uart2 ISR), not reconstructed
 * from disassembly -- see tcs.c's header comment for exactly which
 * pieces came from where and the confidence on each. */

void tcs_init(void);
void tcs_poll(void);

/* Kicks off a scan for `node` (0-255) on the bus: sends the scan frame,
 * then the status frame, and waits (bounded) for a status reply. Returns
 * true and fills *value with the 16-bit status value on success. */
int tcs_scan_node(uint8_t node, uint16_t *value);

/*
 * Phase 3: real Start/Stop/Pause/Resume/Print/GetData transactions,
 * opcodes/payloads ported from the real v3.01 release's
 * TCS_Process_CMD.c (see the port plan's Phase 3 section for exact
 * line references and what's confirmed vs. inferred). Blocking calls,
 * reusing tcs_scan_node()'s send-then-wait-for-ack pattern -- NOT the
 * reference's own async tc_init()/tc_poll() state machine, which this
 * project's synchronous cmd.c/yc1021.c pipeline has no equivalent for.
 *
 * NONE of this has been run against a real TCS meter yet -- see the
 * port plan's Phase 3 verification section for exactly what's still
 * unconfirmed (GetData's reply byte offset, the active_product default).
 */

/* Start delivery: clear-transaction + configure + start (3 frames, fixed
 * ~50ms pacing between them, matching the reference exactly -- no
 * per-step ACK is checked, since the reference itself doesn't check one
 * either). gross_preset_tenths==0 selects direct delivery; non-zero
 * selects preset delivery encoding that value as an IEEE754 double on
 * the wire. Always returns 1 (fire-and-assume-success, matching the
 * reference's own behavior) unless the node argument itself is invalid. */
int tcs_start(uint8_t node, uint32_t gross_preset_tenths);

/* Single-frame commands: send, block waiting (bounded) for any reply.
 * Return 1 if a reply arrived in time, 0 on timeout. */
int tcs_stop(uint8_t node);
int tcs_pause(uint8_t node);
int tcs_resume(uint8_t node);
int tcs_print(uint8_t node);

typedef struct {
    int32_t flow_tenths;
    int32_t gross_tenths;
    int32_t system_gross_tenths;
    int32_t net_totalizer_tenths;
} tcs_reading_t;

/* Polls flowrate/gross-display/system-gross/net-totalizer in sequence
 * (4 blocking round trips). Returns 1 if all 4 replied in time, 0 if any
 * one timed out or came back too short to contain a value (out is left
 * partially filled in that case -- caller should treat 0 as "don't trust
 * *out at all", not "some fields are valid"). */
int tcs_get_data(uint8_t node, tcs_reading_t *out);

/* Pure-integer IEEE754 double <-> tenths-of-a-unit codec, no `double`/
 * `float` C type in the decode direction (matches this project's
 * no-floats convention) -- verified in tests/verify_tcs_double_codec.py
 * before being transcribed here; re-run that if you touch this logic.
 * Handles normal finite doubles in any realistic meter-reading range;
 * returns/encodes 0 for zero/subnormal/Inf/NaN, since real meter
 * readings are never those. */
int32_t tcs_double_bits_to_tenths(const uint8_t wire[8]);
void    tcs_tenths_to_double_bits(int32_t tenths, uint8_t wire[8]);

#endif /* TCS_H */
