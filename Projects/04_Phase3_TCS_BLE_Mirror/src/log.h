#ifndef LOG_H
#define LOG_H

#include <stdint.h>

/* Shared logging: writes to both RTT (over SWD, no extra hardware) and
 * the debug UART (DB9/J3). Call log_init() once after uart clocks/pins
 * are ready (see clock_init()); every other module just calls log_line()
 * or log_hex() directly. */
void log_init(void);
void log_line(const char *s);
void log_hex(const char *prefix, const uint8_t *data, uint32_t len);
void log_uint(uint32_t v);

/*
 * Verbose trace line, gated on cmd.c's SetDbg state (silent no-op when
 * off): "I,<ms>,<file>,<line>:<msg>\r\n" -- same shape as the real
 * PandaBox firmware's own debug-UART trace format. 'level' is a single
 * char, conventionally 'I' or 'D' to match that format.
 */
void log_dbg_raw(char level, const char *file, int line, const char *msg);
#define LOG_DBG(level, msg) log_dbg_raw((level), __FILE__, __LINE__, (msg))

#endif /* LOG_H */
