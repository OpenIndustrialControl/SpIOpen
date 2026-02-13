/**
 * SpIOpen – build TX frames (unified format: preamble, TTL, 11-bit CID + 5 flags, DLC, data, CRC).
 * Used for MISO Chain Bus TX; same format applies to MOSI Drop Bus TX.
 */
#include "spiopen_protocol.h"
#include <stddef.h>
#include <stdint.h>

size_t spiopen_frame_build(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint16_t cid_11bit, uint8_t flags_5bit, const uint8_t *data, size_t data_len)
{
    if (buf == NULL || data_len > SPIOPEN_MAX_PAYLOAD)
        return 0;

    uint8_t dlc_raw = spiopen_byte_count_to_dlc_raw(data_len);
    if (dlc_raw == 0xFF)
        return 0;

    size_t total = (size_t)SPIOPEN_HEADER_LEN + data_len + SPIOPEN_CRC_BYTES;
    if (buf_cap < total)
        return 0;

    buf[0] = SPIOPEN_PREAMBLE;
    buf[1] = ttl;
    /* Bytes 2-3: 11-bit CID + 5 flags, big-endian (high 8 bits, then low 8 bits). */
    uint16_t cid_flags = (uint16_t)((flags_5bit & 0x1Fu) << 11) | (cid_11bit & 0x7FFu);
    buf[2] = (uint8_t)(cid_flags >> 8);
    buf[3] = (uint8_t)(cid_flags & 0xFFu);
    if (spiopen_dlc_encode(dlc_raw, &buf[4]) != 0)
        return 0;

    if (data_len != 0 && data != NULL) {
        for (size_t i = 0; i < data_len; i++)
            buf[SPIOPEN_HEADER_LEN + i] = data[i];
    }
    spiopen_append_crc32(buf, (size_t)SPIOPEN_HEADER_LEN + data_len);
    return total;
}

size_t spiopen_frame_build_std(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint8_t function_code_4bit, uint8_t node_id_7bit, const uint8_t *data, size_t data_len)
{
    uint16_t cid = (uint16_t)((function_code_4bit & 0x0Fu) << 7) | (uint16_t)(node_id_7bit & 0x7Fu);
    return spiopen_frame_build(buf, buf_cap, ttl, cid, 0u, data, data_len);
}
