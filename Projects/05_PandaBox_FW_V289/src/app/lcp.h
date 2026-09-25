/*!
    \file    lcp.h
    \brief   Liquid Controls LCP framing: 7E 7E to from status len data... crcLo crcHi

    - 0x7E and 0x1B after the leading 7E 7E are sent as 1B 7E / 1B 1B (not counted in len)
    - CRC-16: poly 0x1021, seed 0x7E7E, data bits shifted in MSB first, no final XOR;
      covers to..data including inserted escapes, not escapes in front of the CRC bytes
    - multi-byte values are big-endian
*/

#ifndef LCP_H
#define LCP_H

#include <stdint.h>

#define LCP_MAX_DATA        255U
#define LCP_MAX_FRAME       (2U + 2U * (4U + LCP_MAX_DATA + 2U))

#define LCP_HOST_NODE       0x14U   /* PandaBox host address (Leo's firmware) */

#define LCP_ST_MSGID        0x01U
#define LCP_ST_SYNC         0x02U
#define LCP_ST_RESPONSE     0x80U

typedef struct {
    uint8_t to;
    uint8_t from;
    uint8_t status;
    uint8_t len;
    uint8_t data[LCP_MAX_DATA];
} lcp_frame_t;

/* build an escaped frame into out; returns its length */
uint32_t lcp_build(uint8_t *out, uint8_t to, uint8_t from, uint8_t status, const uint8_t *data, uint8_t len);

/* parse one escaped frame (must start with 7E 7E); returns 1 on a valid CRC */
int lcp_parse(const uint8_t *in, uint32_t n, lcp_frame_t *f);

/* host-side CRC self test against the protocol document's vectors; returns number of failures */
int lcp_selftest(void);

#endif /* LCP_H */
