/**
 * CRC-32 (IEEE 802.3) for SpIOpen frames.
 * Used by drop RX and other ports for software verification; chain TX uses
 * the single RP2040 DMA sniffer for hardware CRC.
 */
#include "spiopen_protocol.h"
#include <stddef.h>
#include <stdint.h>

/* Reflected polynomial 0x04C11DB7 → 0xEDB88320. */
#define CRC32_POLY  0xEDB88320u

static uint32_t s_crc32_table[256];

static void crc32_init_table(void)
{
    for (uint32_t n = 0; n < 256u; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1u)
                c = (c >> 1) ^ CRC32_POLY;
            else
                c >>= 1;
        }
        s_crc32_table[n] = c;
    }
}

uint32_t spiopen_crc32(const uint8_t *data, size_t len)
{
    static int once;
    if (!once) {
        crc32_init_table();
        once = 1;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        crc = (crc >> 8) ^ s_crc32_table[(crc ^ (uint32_t)byte) & 0xFFu];
    }
    return crc ^ 0xFFFFFFFFu;
}
