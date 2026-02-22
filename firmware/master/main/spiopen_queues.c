/**
 * SpIOpen master – TX and RX queues.
 */
#include "spiopen_queues.h"
#include "freertos/queue.h"
#include <string.h>

static QueueHandle_t s_tx_queue;
static QueueHandle_t s_rx_queue;

void spiopen_queues_init(void)
{
    s_tx_queue = xQueueCreate(SPIOPEN_QUEUE_DEPTH, sizeof(spiopen_frame_desc_t));
    s_rx_queue = xQueueCreate(SPIOPEN_QUEUE_DEPTH, sizeof(spiopen_frame_desc_t));
    configASSERT(s_tx_queue != NULL && s_rx_queue != NULL);
}

BaseType_t send_to_spiopen_tx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    return xQueueSend(s_tx_queue, &d, xTicksToWait);
}

BaseType_t receive_from_spiopen_tx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait)
{
    return xQueueReceive(s_tx_queue, desc, xTicksToWait);
}

void send_to_spiopen_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    xQueueSendFromISR(s_rx_queue, &d, pxHigherPriorityTaskWoken);
}

BaseType_t send_to_spiopen_rx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    return xQueueSend(s_rx_queue, &d, xTicksToWait);
}

BaseType_t receive_from_spiopen_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait)
{
    return xQueueReceive(s_rx_queue, desc, xTicksToWait);
}
