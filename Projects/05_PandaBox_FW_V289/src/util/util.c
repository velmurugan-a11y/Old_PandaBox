#include <stddef.h>
#include "util.h"

uint32_t UTIL_SwapU32(uint32_t v)
{
    return ((v & 0xFFU) << 24) | ((v & 0xFF00U) << 8) | ((v >> 8) & 0xFF00U) | ((v >> 24) & 0xFFU);
}

int UTIL_IsValidIp(const char *s)
{
    int dots = 0, dig = 0, val = 0;

    if(s == NULL || s[0] == 0) {
        return 0;
    }
    for(; *s; s++) {
        if(*s == '.') {
            if(!dig || val > 255) {
                return 0;
            }
            dots++;
            dig = 0;
            val = 0;
        } else if(*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            dig++;
        } else {
            return 0;
        }
    }
    return (dots == 3 && dig && val <= 255) ? 1 : 0;
}

void UTIL_RemoveStrNewLine(char *s, uint32_t max)
{
    uint32_t i;
    for(i = 0; i + 1 < max && s[i]; i++) {
        if(s[i] == '\r' && s[i + 1] == '\n') {
            s[i] = 0;
            return;
        }
    }
}

uint32_t UTIL_Str2U32(const char *s)
{
    uint32_t v = 0;
    if(s == NULL) {
        return 0;
    }
    while(*s == ' ') {
        s++;
    }
    while(*s >= '0' && *s <= '9') {
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static const char HEX_U[] = "0123456789ABCDEF";

uint32_t UTIL_DataToHexString(char *out, const uint8_t *in, uint32_t n)
{
    uint32_t i;
    for(i = 0; i < n; i++) {
        out[2 * i] = HEX_U[in[i] >> 4];
        out[2 * i + 1] = HEX_U[in[i] & 0x0FU];
    }
    out[2 * n] = 0;
    return 2 * n;
}

static uint8_t hexv(char c)
{
    if(c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if(c >= 'a' && c <= 'f') {
        return (uint8_t)(c - 'a' + 10);
    }
    if(c >= 'A' && c <= 'F') {
        return (uint8_t)(c - 'A' + 10);
    }
    return 0;
}

uint32_t UTIL_HexStringToData(uint8_t *out, const char *in, uint32_t n)
{
    uint32_t i;
    for(i = 0; i + 1 < n; i += 2) {
        out[i / 2] = (uint8_t)((hexv(in[i]) << 4) | hexv(in[i + 1]));
    }
    return n / 2;
}

uint32_t UTIL_ByteSum32(const uint8_t *p, uint32_t len)
{
    uint32_t s = 0, i;
    for(i = 0; i < len; i++) {
        s += p[i];
    }
    return s;
}
