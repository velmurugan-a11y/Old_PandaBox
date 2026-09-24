/*
 * cmd.h -- Lx command dispatcher, transport-agnostic.
 *
 * Owns the non-meter Lx command set (BoxInfo, BoxStatus, BoxTime,
 * RdBtName/SetBtName, RdPortLcrNode/SetPortLcrNode/RdRegister, BoxReset,
 * DeleteAll, BoxStorage, GetLastCmd, GetLastMtrCmd) so the same logic can
 * be reused regardless of which physical link (YC1021 BLE, EC25 4G, debug
 * UART) the command arrived on -- mirrors the real PandaBox v3 firmware's
 * Response_Data() abstraction (see reference/pandabox-v3-full's uart.c).
 *
 * Meter commands (Start, Stop, Pause, GetData/GetDataTs, ModifyLcrNode)
 * are deliberately NOT implemented yet -- no LCR/TCS meter is attached to
 * this board for testing. See gd32f305z-native/README.md.
 */
#ifndef CMD_H
#define CMD_H

#include <stdint.h>

/* Initializes command-dispatcher state (node tables, BT name, mode). */
void cmd_init(void);

/*
 * Parses one Lx command line (CRLF already stripped by the caller, exactly
 * as handed to yc1021.c's dispatch_command) and writes the ASCII response
 * -- including its own trailing "\r\n" -- into out (out_cap bytes).
 * Returns the response length, or 0 if there is nothing to send back
 * (matches SetDbg's real behavior: no ack over this channel).
 */
uint32_t cmd_process_line(const char *line, uint32_t len, char *out, uint32_t out_cap);

/*
 * True after a BoxReset command has been processed (its "LxBoxReset 0\r\n"
 * reply is already sitting in the caller's send buffer). The caller
 * should finish transmitting that reply, then actually reset the MCU --
 * matches the real firmware's "reply first, then reset" ordering.
 */
int cmd_reset_pending(void);

/* True after "SetDbg 1", false after "SetDbg 0" (starts false). Gates
 * log.c's LOG_DBG() verbose trace macro -- see its header comment. */
int cmd_debug_enabled(void);

/* Phase 2 (flash storage): when disabled, Set* commands still update
 * their in-RAM state and ack normally, but skip flash_store_save_settings()
 * -- used by cmd_selftest.c so its exercise of every command doesn't
 * overwrite the real persisted settings with test values. Persistence
 * defaults to enabled. */
void cmd_set_persist_enabled(int enabled);

#endif
