/**
 * SpIOpen protocol – frame layout, CRC-32, DLC (Hamming).
 * Portable; no RTOS/hardware deps.
 */
#ifndef SPIOPEN_PROTOCOL_H
#define SPIOPEN_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Frame layout (see docs/Architecture.md) */
#define SPIOPEN_PREAMBLE      0xAAu
#define SPIOPEN_CRC_BYTES     4
#define SPIOPEN_MAX_PAYLOAD   64

/**
 * When the last 4 bytes of a frame are the correct IEEE 802.3 CRC-32,
 * CRC(entire_frame) equals this residue. Use for software verification on
 * drop RX and other ports. Chain TX uses the single RP2040 DMA sniffer for
 * hardware CRC (full frame in one DMA).
 */
#define SPIOPEN_CRC32_RESIDUE  0xC704DD7Bu

/* ----- DLC (Hamming 8,4) ----- */
uint8_t spiopen_dlc_to_byte_count(uint8_t dlc_raw);
int spiopen_dlc_encode(uint8_t dlc_raw, uint8_t *out_encoded);
int spiopen_dlc_decode(uint8_t encoded, uint8_t *out_dlc_raw);

/* ----- CRC-32 (IEEE 802.3). Use for drop RX / other ports; chain TX uses DMA sniffer. ----- */
uint32_t spiopen_crc32(const uint8_t *data, size_t len);

/**
 * Verify frame CRC: CRC(full_frame) == SPIOPEN_CRC32_RESIDUE.
 * Returns 1 if valid, 0 if invalid or len < SPIOPEN_CRC_BYTES.
 */
static inline int spiopen_crc32_verify_frame(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len < SPIOPEN_CRC_BYTES)
        return 0;
    return spiopen_crc32(frame, len) == SPIOPEN_CRC32_RESIDUE ? 1 : 0;
}

#endif /* SPIOPEN_PROTOCOL_H */
