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
#define SPIOPEN_HEADER_LEN    4   /* TTL, CID+flags (2), DLC; preamble is not in frame buffer */
#define SPIOPEN_CRC_BYTES     4
#define SPIOPEN_MAX_PAYLOAD   64

/* ----- DLC (Hamming 8,4) ----- */
uint8_t spiopen_dlc_to_byte_count(uint8_t dlc_raw);
/** Map payload byte count (0–8, 12, 16, 20, 24, 32, 48, 64) to DLC raw 0–15. Returns 0xFF if invalid. */
uint8_t spiopen_byte_count_to_dlc_raw(size_t byte_count);
int spiopen_dlc_encode(uint8_t dlc_raw, uint8_t *out_encoded);
int spiopen_dlc_decode(uint8_t encoded, uint8_t *out_dlc_raw);

/* ----- CRC-32 (IEEE 802.3). Use for dropbus RX / other ports; chainbus TX uses DMA sniffer. ----- */
uint32_t spiopen_crc32(const uint8_t *data, size_t len);

/**
 * Verify frame CRC: compute CRC over frame[0..len-5] and compare to the
 * last 4 bytes (big-endian, same order as spiopen_append_crc32).
 * Returns 1 if valid, 0 if invalid or len < SPIOPEN_CRC_BYTES.
 */
static inline int spiopen_crc32_verify_frame(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len < SPIOPEN_CRC_BYTES)
        return 0;
    uint32_t computed = spiopen_crc32(frame, len - SPIOPEN_CRC_BYTES);
    uint32_t received = ((uint32_t)frame[len - 4] << 24)
                      | ((uint32_t)frame[len - 3] << 16)
                      | ((uint32_t)frame[len - 2] << 8)
                      | ((uint32_t)frame[len - 1]);
    return (computed == received) ? 1 : 0;
}

/**
 * Append 4-byte IEEE 802.3 CRC-32 to buf[0..len-1], writing at buf[len..len+3].
 * Byte order: big-endian (MSB first). Caller must ensure buf has at least len+4 bytes.
 * Total frame length after call is len + SPIOPEN_CRC_BYTES.
 */
void spiopen_append_crc32(uint8_t *buf, size_t len);

/* CANopen COB-ID for first PDO of node 1 (used for demo/fake PDO). */
#define SPIOPEN_CHAIN_COB_ID_PDO1_NODE1  181u  /* 0x181 */

/**
 * 11-bit CID bit layout: 4-bit command (MSBs) + 7-bit node ID (LSBs).
 * CID = (command << SPIOPEN_CID_COMMAND_SHIFT) | node_id.
 * In the header, bytes 2–3 hold a 16-bit word: bits 0–10 = CID, bits 11–15 = 5 flag bits.
 */
#define SPIOPEN_CID_NODE_SHIFT       0u   /**< Node ID occupies LSBs of the 11-bit CID (7 bits). */
#define SPIOPEN_CID_COMMAND_SHIFT   7u   /**< Command/function code occupies MSBs of the 11-bit CID (4 bits). */
#define SPIOPEN_CID_FLAGS_SHIFT      11u  /**< Protocol flags occupy bits 11–15 of the 16-bit header word (5 bits). */

#define SPIOPEN_CID_NODE_MASK        0x7Fu  /**< Mask for 7-bit node ID (use after shift 0). */
#define SPIOPEN_CID_COMMAND_MASK     0x0Fu  /**< Mask for 4-bit command (use before shift). */
#define SPIOPEN_CID_FLAGS_MASK       0x1Fu  /**< Mask for 5 flag bits (use before shift). */

/** Build 11-bit CID from 4-bit command and 7-bit node ID. Command in MSBs, node in LSBs. */
static inline uint16_t spiopen_cid_from_command_node(uint8_t command_4bit, uint8_t node_id_7bit)
{
    return (uint16_t)((command_4bit & SPIOPEN_CID_COMMAND_MASK) << SPIOPEN_CID_COMMAND_SHIFT)
         | (uint16_t)(node_id_7bit & SPIOPEN_CID_NODE_MASK);
}

/* ----- Frame build (unified format: drop bus and chain bus) ----- */

/**
 * Build a SpIOpen frame into buf (TTL, 11-bit CID + 5 flags, DLC, data, CRC). Preamble is not
 * stored; TX sends preamble separately. Same buffer format on both buses; header is 4 bytes.
 *
 * \param buf          Output buffer (must have space for SPIOPEN_HEADER_LEN + data_len + 4 CRC).
 * \param buf_cap      Capacity of buf in bytes.
 * \param ttl          TTL byte (decremented on chain before retransmit; set by master on drop bus).
 * \param cid_11bit    11-bit CANopen COB-ID (0–2047).
 * \param flags_5bit    Five protocol flag bits (bits 11–15 of the CID+flags word); use 0 for standard frames.
 * \param data         Payload bytes (may be NULL if data_len 0).
 * \param data_len     Payload length 0–64 (must match a valid DLC: 0–8, 12, 16, 20, 24, 32, 48, 64).
 * \return Total buffer length (4 + data_len + 4), or 0 on error.
 */
size_t spiopen_frame_build(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint16_t cid_11bit, uint8_t flags_5bit, const uint8_t *data, size_t data_len);

/**
 * Wrapper: build frame with 11-bit CID from function_code (4 bits) and node_id (7 bits), flags=0.
 * Uses spiopen_cid_from_command_node() so CID = (command << 7) | node_id. Use for standard PDO/SDO-style frames.
 * \return Total frame length, or 0 on error.
 */
size_t spiopen_frame_build_std(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint8_t function_code_4bit, uint8_t node_id_7bit, const uint8_t *data, size_t data_len);

#endif /* SPIOPEN_PROTOCOL_H */
