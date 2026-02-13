/**
 * Hamming(8,4) encode/decode for SpIOpen DLC byte.
 * The 4-bit DLC (0–15) follows CAN-FD length code; it is protected by an
 * extended Hamming code so we can correct one bit error and detect two.
 *
 * Bit layout (1-indexed): P1 P2 D1 P3 D2 D3 D4 P4
 * - Parity at positions 1,2,4,8 (0-indexed: 0,1,3,7)
 * - Data D1..D4 at positions 3,5,6,7 (0-indexed: 2,4,5,6)
 * - P4 is overall even parity (SECDED).
 */
#include "spiopen_protocol.h"
#include <stdint.h>

/* CAN-FD DLC (0–15) → data byte count (0–64). */
static const uint8_t s_dlc_to_bytes[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,  /* 0-8 */
    12, 16, 20, 24, 32, 48, 64  /* 9-15 */
};

uint8_t spiopen_dlc_to_byte_count(uint8_t dlc_raw)
{
    if (dlc_raw > 15u)
        return 0;
    return s_dlc_to_bytes[dlc_raw];
}

/* Parity groups (even parity). Bit indices 0..7. */
#define B(c, i)  (((c) >> (i)) & 1u)
#define S1(c)    (B(c,0) ^ B(c,2) ^ B(c,4) ^ B(c,6))
#define S2(c)    (B(c,1) ^ B(c,2) ^ B(c,5) ^ B(c,6))
#define S3(c)    (B(c,3) ^ B(c,4) ^ B(c,5) ^ B(c,6))
#define S4(c)    (B(c,0) ^ B(c,1) ^ B(c,2) ^ B(c,3) ^ B(c,4) ^ B(c,5) ^ B(c,6) ^ B(c,7))

/** Extract 4-bit data nibble from corrected codeword (bits 2,4,5,6). */
static inline uint8_t data_nibble(uint8_t c)
{
    return (uint8_t)(((c >> 2) & 1u) | ((c >> 4) & 1u) << 1 | ((c >> 5) & 1u) << 2 | ((c >> 6) & 1u) << 3);
}

/** Flip bit at 0-indexed position. */
static inline uint8_t flip_bit(uint8_t c, unsigned pos)
{
    return (uint8_t)(c ^ (1u << pos));
}

int spiopen_dlc_encode(uint8_t dlc_raw, uint8_t *out_encoded)
{
    if (out_encoded == NULL || dlc_raw > 15u)
        return -1;
    /* Data bits at positions 2,4,5,6. */
    uint8_t d1 = (dlc_raw >> 0) & 1u;
    uint8_t d2 = (dlc_raw >> 1) & 1u;
    uint8_t d3 = (dlc_raw >> 2) & 1u;
    uint8_t d4 = (dlc_raw >> 3) & 1u;
    uint8_t p1 = d1 ^ d2 ^ d4;
    uint8_t p2 = d1 ^ d3 ^ d4;
    uint8_t p3 = d2 ^ d3 ^ d4;
    uint8_t c = (uint8_t)(p1 | (p2 << 1) | (d1 << 2) | (p3 << 3) | (d2 << 4) | (d3 << 5) | (d4 << 6));
    uint8_t p4 = B(c,0) ^ B(c,1) ^ B(c,2) ^ B(c,3) ^ B(c,4) ^ B(c,5) ^ B(c,6);
    c |= (p4 << 7);
    *out_encoded = c;
    return 0;
}

int spiopen_dlc_decode(uint8_t encoded, uint8_t *out_dlc_raw)
{
    if (out_dlc_raw == NULL)
        return -1;

    uint8_t s1 = S1(encoded);
    uint8_t s2 = S2(encoded);
    uint8_t s3 = S3(encoded);
    uint8_t s4 = S4(encoded);

    uint8_t syndrome = (uint8_t)(s1 | (s2 << 1) | (s3 << 2));
    uint8_t c = encoded;

    if (syndrome != 0) {
        if (s4 != 0) {
            /* Single-bit error: syndrome (1–7) gives 0-indexed position to flip. */
            if (syndrome >= 1u && syndrome <= 7u)
                c = flip_bit(c, (unsigned)(syndrome - 1u));
            /* syndrome 0 shouldn't happen with s4=1; treat as uncorrectable */
        } else {
            /* Two bits different → double error, detect only. */
            return -1;
        }
    } else if (s4 != 0) {
        /* Syndrome zero but overall parity wrong → error in bit 7 (P4). */
        c = flip_bit(c, 7);
    }

    *out_dlc_raw = data_nibble(c);
    return 0;
}
