/*!
    \file    lcp.c
    \brief   Liquid Controls LCP framing, escaping and CRC
*/

#include <string.h>
#include "lcp.h"

#define LCP_SYNC    0x7EU
#define LCP_ESC     0x1BU

static void crc_byte(uint16_t *crc, uint8_t b)
{
    int i;

    for(i = 7; i >= 0; i--) {
        uint16_t carry = (uint16_t)(*crc & 0x8000U);
        *crc = (uint16_t)((*crc << 1) | ((b >> i) & 1U));
        if(carry) {
            *crc ^= 0x1021U;
        }
    }
}

static uint32_t put_escaped(uint8_t *out, uint32_t n, uint8_t b, uint16_t *crc)
{
    if(b == LCP_SYNC || b == LCP_ESC) {
        out[n++] = LCP_ESC;
        if(crc) {
            crc_byte(crc, LCP_ESC);
        }
    }
    out[n++] = b;
    if(crc) {
        crc_byte(crc, b);
    }
    return n;
}

uint32_t lcp_build(uint8_t *out, uint8_t to, uint8_t from, uint8_t status, const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0x7E7EU;
    uint32_t n = 0U, i;

    out[n++] = LCP_SYNC;
    out[n++] = LCP_SYNC;
    n = put_escaped(out, n, to, &crc);
    n = put_escaped(out, n, from, &crc);
    n = put_escaped(out, n, status, &crc);
    n = put_escaped(out, n, len, &crc);
    for(i = 0U; i < len; i++) {
        n = put_escaped(out, n, data[i], &crc);
    }
    n = put_escaped(out, n, (uint8_t)(crc & 0xFFU), NULL);
    n = put_escaped(out, n, (uint8_t)(crc >> 8), NULL);
    return n;
}

int lcp_parse(const uint8_t *in, uint32_t n, lcp_frame_t *f)
{
    uint8_t hdr[4], crcb[2];
    uint16_t crc = 0x7E7EU;
    uint32_t i = 2U, k, want;

    if(n < 8U || in[0] != LCP_SYNC || in[1] != LCP_SYNC) {
        return 0;
    }
    /* header + data: escapes count in the CRC */
    for(k = 0U, want = 4U; k < want; k++) {
        if(i >= n) {
            return 0;
        }
        if(in[i] == LCP_ESC) {
            crc_byte(&crc, LCP_ESC);
            i++;
            if(i >= n) {
                return 0;
            }
        }
        crc_byte(&crc, in[i]);
        if(k < 4U) {
            hdr[k] = in[i];
            if(k == 3U) {
                want = 4U + hdr[3];
            }
        } else {
            f->data[k - 4U] = in[i];
        }
        i++;
    }
    /* CRC bytes: escapes not counted */
    for(k = 0U; k < 2U; k++) {
        if(i >= n) {
            return 0;
        }
        if(in[i] == LCP_ESC) {
            i++;
            if(i >= n) {
                return 0;
            }
        }
        crcb[k] = in[i++];
    }
    if(crcb[0] != (uint8_t)(crc & 0xFFU) || crcb[1] != (uint8_t)(crc >> 8)) {
        return 0;
    }
    f->to = hdr[0];
    f->from = hdr[1];
    f->status = hdr[2];
    f->len = hdr[3];
    return 1;
}

int lcp_selftest(void)
{
    static const struct {
        uint8_t to, from, st, len;
        uint8_t data[4];
        uint8_t expect[12];
        uint8_t elen;
    } v[] = {
        /* protocol document / PandaBox captures */
        {0xFA, 0xFF, 0x02, 1, {0x00}, {0x7E, 0x7E, 0xFA, 0xFF, 0x02, 0x01, 0x00, 0x2F, 0x34}, 9},
        {0x01, 0x14, 0x02, 1, {0x00}, {0x7E, 0x7E, 0x01, 0x14, 0x02, 0x01, 0x00, 0xC4, 0xEB}, 9},
        {0xFA, 0xFF, 0x00, 2, {0x20, 0x02}, {0x7E, 0x7E, 0xFA, 0xFF, 0x00, 0x02, 0x20, 0x02, 0xD4, 0x2F}, 10},
        {0x02, 0x14, 0x01, 2, {0x20, 0x02}, {0x7E, 0x7E, 0x02, 0x14, 0x01, 0x02, 0x20, 0x02, 0xAB, 0x56}, 10},
        /* field 27 = 0x1B must be escaped */
        {0x01, 0x14, 0x01, 2, {0x20, 0x1B}, {0x7E, 0x7E, 0x01, 0x14, 0x01, 0x02, 0x20, 0x1B, 0x1B, 0xFA, 0x66}, 11},
    };
    uint8_t out[32];
    lcp_frame_t f;
    uint32_t i, n;
    int fails = 0;

    for(i = 0U; i < sizeof(v) / sizeof(v[0]); i++) {
        n = lcp_build(out, v[i].to, v[i].from, v[i].st, v[i].data, v[i].len);
        if(n != v[i].elen || memcmp(out, v[i].expect, n) != 0) {
            fails++;
            continue;
        }
        if(!lcp_parse(out, n, &f) || f.to != v[i].to || f.len != v[i].len ||
           memcmp(f.data, v[i].data, f.len) != 0) {
            fails++;
        }
    }
    return fails;
}
