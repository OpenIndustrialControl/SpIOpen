/**
 * SpIOpen ↔ CANopenNode conversion (parse RX, build TX).
 */
#include "spiopen_canopen.h"
#include <string.h>

int spiopen_frame_to_canopen_rx(const uint8_t *buf, size_t len, CO_CANrxMsg_t *out_msg)
{
    if (out_msg == NULL || buf == NULL)
        return 0;
    /* Caller always passes full frame buffer: first 2 bytes are preamble, then content (TTL..CRC). */
    if (len < SPIOPEN_PREAMBLE_BYTES + SPIOPEN_HEADER_LEN + SPIOPEN_CRC_BYTES)
        return 0;
    const uint8_t *frame = buf + SPIOPEN_FRAME_CONTENT_OFFSET;
    const size_t content_len = len - SPIOPEN_PREAMBLE_BYTES;
    if (!spiopen_crc32_verify_frame(frame, content_len))
        return 0;

    uint16_t ident = spiopen_header_read_cid_ident(frame);
    uint8_t dlc_encoded = frame[SPIOPEN_HEADER_OFFSET_DLC];
    uint8_t dlc_raw;
    if (spiopen_dlc_decode(dlc_encoded, &dlc_raw) != 0)
        return 0;
    uint8_t payload_len = spiopen_dlc_to_byte_count(dlc_raw);
    size_t payload_offset = (size_t)SPIOPEN_HEADER_LEN;
    if (content_len < payload_offset + (size_t)payload_len + SPIOPEN_CRC_BYTES)
        return 0;
    if (payload_len > 8u)
        payload_len = 8u;

    out_msg->ident = ident;
    out_msg->DLC = payload_len;
    for (uint8_t i = 0; i < payload_len && i < 8u; i++)
        out_msg->data[i] = frame[payload_offset + i];

    return 1;
}

size_t spiopen_frame_from_canopen_tx(uint16_t ident, uint8_t dlc, const uint8_t *data,
                                    uint8_t *buf, size_t buf_cap, uint8_t ttl)
{
    if (buf == NULL)
        return 0;
    if (dlc > 8u)
        dlc = 8u;
    buf[0] = SPIOPEN_PREAMBLE;
    buf[1] = SPIOPEN_PREAMBLE;
    size_t content = spiopen_frame_build(buf, buf_cap, ttl, ident & 0x07FFu, 0u, data, (size_t)dlc);
    return content == 0 ? 0 : SPIOPEN_PREAMBLE_BYTES + content;
}
