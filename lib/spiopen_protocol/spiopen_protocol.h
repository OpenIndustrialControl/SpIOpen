/**
 * SpIOpen protocol – frame layout, CRC-32, DLC (Hamming).
 * Portable; no RTOS/hardware deps.
 */
#ifndef SPIOPEN_PROTOCOL_H
#define SPIOPEN_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Frame layout (see docs/Architecture.md). Same on both drop bus and chain bus. */
#define SPIOPEN_PREAMBLE      0xAAu
#define SPIOPEN_HEADER_LEN    5   /* preamble, TTL, CID+flags (2), DLC */
#define SPIOPEN_CRC_BYTES     4
#define SPIOPEN_MAX_PAYLOAD   64

/**
 * When the last 4 bytes of a frame are the correct IEEE 802.3 CRC-32,
 * CRC(entire_frame) equals this residue. Use for software verification on
 * dropbus RX and other ports. Chainbus TX uses the single RP2040 DMA sniffer for
 * hardware CRC (full frame in one DMA).
 */
#define SPIOPEN_CRC32_RESIDUE  0xC704DD7Bu

/* ----- DLC (Hamming 8,4) ----- */
uint8_t spiopen_dlc_to_byte_count(uint8_t dlc_raw);
/** Map payload byte count (0–8, 12, 16, 20, 24, 32, 48, 64) to DLC raw 0–15. Returns 0xFF if invalid. */
uint8_t spiopen_byte_count_to_dlc_raw(size_t byte_count);
int spiopen_dlc_encode(uint8_t dlc_raw, uint8_t *out_encoded);
int spiopen_dlc_decode(uint8_t encoded, uint8_t *out_dlc_raw);

/* ----- CRC-32 (IEEE 802.3). Use for dropbus RX / other ports; chainbus TX uses DMA sniffer. ----- */
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

/**
 * Append 4-byte IEEE 802.3 CRC-32 to buf[0..len-1], writing at buf[len..len+3].
 * Byte order: big-endian (MSB first). Caller must ensure buf has at least len+4 bytes.
 * Total frame length after call is len + SPIOPEN_CRC_BYTES.
 */
void spiopen_append_crc32(uint8_t *buf, size_t len);

/* CANopen COB-ID for first PDO of node 1 (used for demo/fake PDO). */
#define SPIOPEN_CHAIN_COB_ID_PDO1_NODE1  181u  /* 0x181 */

/* ----- Frame build (unified format: drop bus and chain bus) ----- */

/**
 * Build a SpIOpen frame into buf (preamble, TTL, 11-bit CID + 5 flags, DLC, data, CRC).
 * Same format on both MOSI Drop Bus and MISO Chain Bus; header is always 5 bytes.
 *
 * \param buf          Output buffer (must have space for SPIOPEN_HEADER_LEN + data_len + 4 CRC).
 * \param buf_cap      Capacity of buf in bytes.
 * \param ttl          TTL byte (decremented on chain before retransmit; set by master on drop bus).
 * \param cid_11bit    11-bit CANopen COB-ID (0–2047).
 * \param flags_5bit    Five protocol flag bits (bits 11–15 of the CID+flags word); use 0 for standard frames.
 * \param data         Payload bytes (may be NULL if data_len 0).
 * \param data_len     Payload length 0–64 (must match a valid DLC: 0–8, 12, 16, 20, 24, 32, 48, 64).
 * \return Total frame length (5 + data_len + 4), or 0 on error.
 */
size_t spiopen_frame_build(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint16_t cid_11bit, uint8_t flags_5bit, const uint8_t *data, size_t data_len);

/**
 * Wrapper: build frame with 11-bit COB-ID from function_code (4 bits) and node_id (7 bits), flags=0.
 * COB-ID = (function_code << 7) | node_id. Use for standard PDO/SDO-style frames.
 * \return Total frame length, or 0 on error.
 */
size_t spiopen_frame_build_std(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint8_t function_code_4bit, uint8_t node_id_7bit, const uint8_t *data, size_t data_len);

#endif /* SPIOPEN_PROTOCOL_H */
