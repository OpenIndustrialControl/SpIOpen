/**
 * SpIOpen slave – descriptor queues for dropbus RX, chainbus RX, and chainbus TX.
 */
#ifndef BUS_QUEUES_H
#define BUS_QUEUES_H

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"

typedef struct {
    uint8_t *buf;
    uint8_t  len;
} spiopen_frame_desc_t;

#define BUS_QUEUE_DEPTH  8

/**
 * Initialize the three queues. Call once after frame_pool_init.
 */
void bus_queues_init(void);

/* ----- dropbus_rx_queue: producer = Dropbus RX (task or ISR), consumer = app task ----- */
void send_to_dropbus_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t send_to_dropbus_rx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait);
BaseType_t receive_from_dropbus_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait);

/* ----- chainbus_rx_queue: producer = Chainbus RX DMA (ISR), consumer = TTL task ----- */
void send_to_chainbus_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t receive_from_chainbus_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait);

/* ----- chainbus_tx_queue: producers = app task, TTL task; consumer = chainbus TX path ----- */
BaseType_t send_to_chainbus_tx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait);
BaseType_t receive_from_chainbus_tx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait);

#endif /* BUS_QUEUES_H */
