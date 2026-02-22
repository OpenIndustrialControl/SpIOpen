/**
 * SpIOpen master – CANopenNode driver over SpIOpen.
 * TX: CO_CANsend enqueues to SpIOpen TX (drop bus). RX: frames from chain bus
 * are dequeued and injected via spiopen_can_driver_inject_rx().
 */
#ifndef SPIOPEN_CAN_DRIVER_H
#define SPIOPEN_CAN_DRIVER_H

#include <stdint.h>
#include <stddef.h>

/** Default TTL for master-originated frames on drop bus. */
#define SPIOPEN_CAN_TTL_DEFAULT  127u

/**
 * Inject a received SpIOpen frame (from chain bus RX queue) into CANopenNode.
 * Call from the CANopen task after receive_from_spiopen_rx().
 *
 * Return convention (standard C): 0 = success, non-zero = failure.
 * - On success: the frame is parsed, dispatched to CANopen RX callbacks, and the
 *   buffer is returned to the frame pool by this function; caller must not free.
 * - On failure: buffer is not freed; caller must call frame_pool_put(buf).
 *
 * \param buf  Frame buffer (header + payload + CRC)
 * \param len  Total length (>= 4 header + 4 CRC)
 * \return 0 on success, non-zero on error (invalid args, parse/CRC failure, or no CAN module)
 */
int spiopen_can_driver_inject_rx(uint8_t *buf, size_t len);

/** Get the CAN module pointer for CO_init. Call after spiopen_can_driver_init(). */
void *spiopen_can_driver_get_CANmodule(void);

/** Initialize the driver (mutex). Call before CO_CANmodule_init(). */
void spiopen_can_driver_init(void);

/** Implement CO_lock_can_send / CO_unlock_can_send (used by CO_driver_target.h). */
void CO_lock_can_send(void);
void CO_unlock_can_send(void);

#endif /* SPIOPEN_CAN_DRIVER_H */
