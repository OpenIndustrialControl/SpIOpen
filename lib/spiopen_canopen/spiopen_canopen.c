/**
 * SpIOpen ↔ CANopenNode conversion (parse RX, build TX).
 */
#include "spiopen_canopen.h"
#include <string.h>

int spiopen_frame_to_canopen_rx(const uint8_t *buf, size_t len, CO_CANrxMsg_t *out_msg)
{
    if (out_msg == NULL || buf == NULL || len < SPIOPEN_HEADER_LEN + SPIOPEN_CRC_BYTES)
        return 0;
    if (!spiopen_crc32_verify_frame(buf, len))
        return 0;

    uint16_t cid_flags = (uint16_t)((uint16_t)buf[1] << 8) | (uint16_t)buf[2];
    uint16_t ident = cid_flags & 0x07FFu;
    uint8_t dlc_encoded = buf[3];
    uint8_t dlc_raw;
    if (spiopen_dlc_decode(dlc_encoded, &dlc_raw) != 0)
        return 0;
    uint8_t payload_len = spiopen_dlc_to_byte_count(dlc_raw);
    size_t payload_offset = (size_t)SPIOPEN_HEADER_LEN;
    if (len < payload_offset + (size_t)payload_len + SPIOPEN_CRC_BYTES)
        return 0;
    if (payload_len > 8u)
        payload_len = 8u;

    out_msg->ident = ident;
    out_msg->DLC = payload_len;
    for (uint8_t i = 0; i < payload_len && i < 8u; i++)
        out_msg->data[i] = buf[payload_offset + i];

    return 1;
}

size_t spiopen_frame_from_canopen_tx(uint16_t ident, uint8_t dlc, const uint8_t *data,
                                    uint8_t *buf, size_t buf_cap, uint8_t ttl)
{
    if (buf == NULL)
        return 0;
    if (dlc > 8u)
        dlc = 8u;
    return spiopen_frame_build(buf, buf_cap, ttl, ident & 0x07FFu, 0u, data, (size_t)dlc);
}
