/*
 * util.h - small helpers Leo's code uses (hex, byte swap, IP check, CRC helpers).
 */
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

uint32_t UTIL_SwapU32(uint32_t v);
int      UTIL_IsValidIp(const char *s);
void     UTIL_RemoveStrNewLine(char *s, uint32_t max);
uint32_t UTIL_Str2U32(const char *s);
uint32_t UTIL_DataToHexString(char *out, const uint8_t *in, uint32_t n);   /* upper-case, NUL-terminated */
uint32_t UTIL_HexStringToData(uint8_t *out, const char *in, uint32_t n);   /* n hex chars -> n/2 bytes */
uint32_t UTIL_ByteSum32(const uint8_t *p, uint32_t len);                   /* APP checksum */

#endif
