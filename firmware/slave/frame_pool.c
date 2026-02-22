/**
 * SpIOpen slave – frame buffer pool implementation.
 */
#include "frame_pool.h"
#include "queue.h"
#include <stddef.h>

static uint8_t s_buffers[FRAME_POOL_SIZE][SPIOPEN_FRAME_BUF_SIZE];
static QueueHandle_t s_free_queue;

void frame_pool_init(void)
{
    configASSERT(FRAME_POOL_SIZE > 0u);
    configASSERT(SPIOPEN_FRAME_BUF_SIZE > 0u);

    s_free_queue = xQueueCreate(FRAME_POOL_SIZE, sizeof(uint8_t *));
    configASSERT(s_free_queue != NULL);

    for (size_t i = 0; i < FRAME_POOL_SIZE; i++) {
        uint8_t *ptr = s_buffers[i];
        configASSERT(ptr != NULL);
        ptr[0] = SPIOPEN_PREAMBLE;
        ptr[1] = SPIOPEN_PREAMBLE;
        BaseType_t ok = xQueueSend(s_free_queue, &ptr, 0);
        configASSERT(ok == pdTRUE);
    }
}

uint8_t *frame_pool_get(void)
{
    uint8_t *buf = NULL;
    if (xQueueReceive(s_free_queue, &buf, 0) == pdTRUE)
        return buf;
    return NULL;
}

uint8_t *frame_pool_get_from_isr(BaseType_t *pxHigherPriorityTaskWoken)
{
    uint8_t *buf = NULL;
    if (xQueueReceiveFromISR(s_free_queue, &buf, pxHigherPriorityTaskWoken) == pdTRUE)
        return buf;
    return NULL;
}

void frame_pool_put(uint8_t *buf)
{
    if (buf == NULL)
        return;
    /* Validate that buf is within our pool (optional safety check). */
    const uintptr_t base = (uintptr_t)&s_buffers[0][0];
    const uintptr_t end  = (uintptr_t)&s_buffers[FRAME_POOL_SIZE - 1][SPIOPEN_FRAME_BUF_SIZE - 1] + 1;
    const uintptr_t p    = (uintptr_t)buf;
    if (p >= base && p < base + (uintptr_t)(FRAME_POOL_SIZE * SPIOPEN_FRAME_BUF_SIZE)) {
        xQueueSend(s_free_queue, &buf, portMAX_DELAY);
    }
}

void frame_pool_put_from_isr(uint8_t *buf, BaseType_t *pxHigherPriorityTaskWoken)
{
    if (buf == NULL)
        return;
    const uintptr_t base = (uintptr_t)&s_buffers[0][0];
    if ((uintptr_t)buf >= base && (uintptr_t)buf < base + (uintptr_t)(FRAME_POOL_SIZE * SPIOPEN_FRAME_BUF_SIZE))
        xQueueSendFromISR(s_free_queue, &buf, pxHigherPriorityTaskWoken);
}
