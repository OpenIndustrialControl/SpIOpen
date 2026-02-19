/**
 * CANopen subsystem init and task (SpIOpen slave).
 * Call canopen_init() after spiopen_can_driver_init(); it creates the CANopen task.
 */
#ifndef SPIOPEN_SLAVE_CANOPEN_H
#define SPIOPEN_SLAVE_CANOPEN_H

/**
 * Initialize CANopen stack and create the CANopen task.
 * Requires: frame_pool, bus_queues, spiopen_can_driver, led_rgb_pwm already initialized.
 *
 * \return 0 on success, non-zero on failure (e.g. CO_new or CO_CANinit failed).
 */
int canopen_init(void);

#endif /* SPIOPEN_SLAVE_CANOPEN_H */
