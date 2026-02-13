/**
 * SpIOpen slave – frame buffer pool for the three ports.
 * One pool of fixed-size buffers; get/put from tasks or ISRs.
 */
#ifndef FRAME_POOL_H
#define FRAME_POOL_H

#include <stdint.h>
#include "FreeRTOS.h"

/* Max frame length: chainbus format 7 + 64 + 4 = 75; round up for alignment. */
#define SPIOPEN_FRAME_BUF_SIZE  80

#define FRAME_POOL_SIZE         16

/**
 * Initialize the buffer pool. Call once before using get/put.
 */
void frame_pool_init(void);

/**
 * Get a buffer from the pool. Non-blocking; safe from task or ISR.
 * Returns NULL if no buffer is free.
 */
uint8_t *frame_pool_get(void);

/**
 * Get a buffer from the pool from an ISR. Sets *pxHigherPriorityTaskWoken
 * if a task should be woken (e.g. when returning a buffer).
 */
uint8_t *frame_pool_get_from_isr(BaseType_t *pxHigherPriorityTaskWoken);

/**
 * Return a buffer to the pool. Call from task context.
 */
void frame_pool_put(uint8_t *buf);

/**
 * Return a buffer to the pool from an ISR.
 */
void frame_pool_put_from_isr(uint8_t *buf, BaseType_t *pxHigherPriorityTaskWoken);

#endif /* FRAME_POOL_H */
