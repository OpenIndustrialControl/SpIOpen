/**
 * SpIOpen master – frame buffer pool.
 */
#include "frame_pool.h"
#include "freertos/queue.h"
#include <stddef.h>

static uint8_t s_buffers[FRAME_POOL_SIZE][SPIOPEN_FRAME_BUF_SIZE];
static QueueHandle_t s_free_queue;

void frame_pool_init(void)
{
    s_free_queue = xQueueCreate(FRAME_POOL_SIZE, sizeof(uint8_t *));
    configASSERT(s_free_queue != NULL);
    for (size_t i = 0; i < FRAME_POOL_SIZE; i++) {
        uint8_t *ptr = s_buffers[i];
        xQueueSend(s_free_queue, &ptr, 0);
    }
}

uint8_t *frame_pool_get(void)
{
    uint8_t *buf = NULL;
    if (xQueueReceive(s_free_queue, &buf, 0) == pdTRUE)
        return buf;
    return NULL;
}

void frame_pool_put(uint8_t *buf)
{
    if (buf == NULL)
        return;
    const uintptr_t base = (uintptr_t)&s_buffers[0][0];
    const uintptr_t end  = (uintptr_t)(FRAME_POOL_SIZE * SPIOPEN_FRAME_BUF_SIZE);
    if ((uintptr_t)buf >= base && (uintptr_t)buf < base + end)
        xQueueSend(s_free_queue, &buf, portMAX_DELAY);
}
