/**
 * SpIOpen ↔ CANopenNode integration (shared by slave and master).
 * Provides CANopenNode-compatible RX message type and conversion between
 * SpIOpen frame buffers and that representation (no RTOS/hardware deps).
 */
#ifndef SPIOPEN_CANOPEN_H
#define SPIOPEN_CANOPEN_H

#include "spiopen_protocol.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CANopenNode-compatible received CAN message.
 * Layout matches what CANopenNode RX callbacks expect (CO_CANrxMsg_read*).
 * Use for both slave (inject into stack) and master (parse from SpIOpen, then pass to stack).
 */
typedef struct {
    uint16_t ident;   /* 11-bit CAN identifier (SpIOpen CID). */
    uint8_t  DLC;     /* Data length code: payload length in bytes (0–8). */
    uint8_t  data[8]; /* Payload; only first DLC bytes are valid. */
} CO_CANrxMsg_t;

#define CO_CANrxMsg_readIdent(msg)  (((CO_CANrxMsg_t *)(msg))->ident)
#define CO_CANrxMsg_readDLC(msg)    (((CO_CANrxMsg_t *)(msg))->DLC)
#define CO_CANrxMsg_readData(msg)   (((CO_CANrxMsg_t *)(msg))->data)

/**
 * Parse a SpIOpen frame into a CANopenNode RX message.
 * Verifies CRC and header, decodes DLC, copies up to 8 payload bytes.
 *
 * \param buf     SpIOpen frame (header + payload + CRC)
 * \param len     Total length (must be >= SPIOPEN_HEADER_LEN + SPIOPEN_CRC_BYTES)
 * \param out_msg Filled on success (ident, DLC, data[0..DLC-1])
 * \return 1 on success, 0 on CRC/length/DLC error
 */
int spiopen_frame_to_canopen_rx(const uint8_t *buf, size_t len, CO_CANrxMsg_t *out_msg);

/**
 * Build a SpIOpen frame from CANopen TX data (ident, DLC, payload).
 * For use by slave CO_CANsend or master when sending a CAN frame over SpIOpen.
 *
 * \param ident   11-bit CAN identifier (COB-ID)
 * \param dlc     Data length 0–8
 * \param data    Payload (may be NULL if dlc 0)
 * \param buf     Output buffer
 * \param buf_cap Buffer capacity
 * \param ttl     TTL byte (e.g. 127 for default)
 * \return Total frame length, or 0 on error
 */
size_t spiopen_frame_from_canopen_tx(uint16_t ident, uint8_t dlc, const uint8_t *data,
                                     uint8_t *buf, size_t buf_cap, uint8_t ttl);

#ifdef __cplusplus
}
#endif

#endif /* SPIOPEN_CANOPEN_H */
