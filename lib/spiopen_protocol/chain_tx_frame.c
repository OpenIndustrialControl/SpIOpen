/**
 * SpIOpen – build TX frame content at buf[SPIOPEN_FRAME_CONTENT_OFFSET..].
 * Caller must set buf[0..1] = 0xAA (preamble). CRC is over content only; preamble not in checksum.
 * \return Content length (SPIOPEN_HEADER_LEN + data_len + SPIOPEN_CRC_BYTES), or 0 on error.
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

    size_t content_len = (size_t)SPIOPEN_HEADER_LEN + data_len + SPIOPEN_CRC_BYTES;
    if (buf_cap < SPIOPEN_PREAMBLE_BYTES + content_len)
        return 0;

    uint8_t *const content = buf + SPIOPEN_FRAME_CONTENT_OFFSET;
    content[0] = ttl;
    content[SPIOPEN_HEADER_OFFSET_CID_HIGH] = (uint8_t)((flags_5bit & SPIOPEN_CID_FLAGS_MASK) << SPIOPEN_CID_FLAGS_SHIFT);
    spiopen_header_write_cid_ident(content, cid_11bit);
    if (spiopen_dlc_encode(dlc_raw, &content[SPIOPEN_HEADER_OFFSET_DLC]) != 0)
        return 0;

    if (data_len != 0 && data != NULL) {
        for (size_t i = 0; i < data_len; i++)
            content[SPIOPEN_HEADER_LEN + i] = data[i];
    }
    spiopen_append_crc32(content, (size_t)SPIOPEN_HEADER_LEN + data_len);
    return content_len;
}

size_t spiopen_frame_build_std(uint8_t *buf, size_t buf_cap, uint8_t ttl, uint8_t function_code_4bit, uint8_t node_id_7bit, const uint8_t *data, size_t data_len)
{
    uint16_t cid = spiopen_cid_from_command_node(function_code_4bit, node_id_7bit);
    return spiopen_frame_build(buf, buf_cap, ttl, cid, 0u, data, data_len);
}
