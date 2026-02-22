/**
 * SpIOpen master – TX and RX queues between driver/transport and CANopen task.
 */
#ifndef SPIOPEN_QUEUES_H
#define SPIOPEN_QUEUES_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"

typedef struct {
    uint8_t *buf;
    uint8_t  len;
} spiopen_frame_desc_t;

#define SPIOPEN_QUEUE_DEPTH  8

void spiopen_queues_init(void);

/* SpIOpen TX: CO_driver enqueues; spiopen_tx task dequeues and sends on SPI master */
BaseType_t send_to_spiopen_tx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait);
BaseType_t receive_from_spiopen_tx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait);

/* SpIOpen RX: spiopen_rx (SPI slave) enqueues; CANopen task dequeues and injects */
void send_to_spiopen_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t send_to_spiopen_rx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait);
BaseType_t receive_from_spiopen_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait);

#endif /* SPIOPEN_QUEUES_H */
