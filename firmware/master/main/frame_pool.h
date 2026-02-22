/**
 * SpIOpen master – frame buffer pool for SpIOpen TX and RX.
 */
#ifndef FRAME_POOL_H
#define FRAME_POOL_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"

#define SPIOPEN_FRAME_BUF_SIZE  80
#define FRAME_POOL_SIZE         16

void frame_pool_init(void);
uint8_t *frame_pool_get(void);
void frame_pool_put(uint8_t *buf);

#endif /* FRAME_POOL_H */
