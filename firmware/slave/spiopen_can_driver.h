/**
 * SpIOpen transport driver for CANopenNode.
 * Injects received SpIOpen frames into the stack and implements CO_CANsend via chainbus_tx.
 */
#ifndef SPIOPEN_CAN_DRIVER_H
#define SPIOPEN_CAN_DRIVER_H

#include <stdint.h>
#include <stddef.h>

/** Default TTL for slave-originated frames on chain bus. */
#define SPIOPEN_CAN_TTL_DEFAULT  127u

/**
 * Inject a received SpIOpen frame into CANopenNode.
 * Call from the CANopen task after receive_from_dropbus_rx().
 * Parses header (TTL, CID, DLC), verifies CRC, matches rxArray and calls CANrx_callback.
 * Returns the buffer to the frame pool when done.
 * \param buf  Frame buffer (header + payload + CRC)
 * \param len  Total length (>= 4 header + 4 CRC)
 * \return 1 if CRC valid and frame was processed, 0 if CRC invalid or len too short (caller should still put buf back)
 */
int spiopen_can_driver_inject_rx(uint8_t *buf, size_t len);

/**
 * Get the CAN module pointer for CO_init etc. Call after spiopen_can_driver_init().
 */
void *spiopen_can_driver_get_CANmodule(void);

/**
 * Initialize the driver (mutex, etc.). Call before CO_CANmodule_init().
 */
void spiopen_can_driver_init(void);

/** Implement CO_lock_can_send / CO_unlock_can_send (used by CO_driver_target.h). */
void CO_lock_can_send(void);
void CO_unlock_can_send(void);

#endif /* SPIOPEN_CAN_DRIVER_H */
