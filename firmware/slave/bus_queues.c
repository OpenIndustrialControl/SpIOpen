/**
 * SpIOpen slave – descriptor queues implementation.
 */
#include "bus_queues.h"
#include "queue.h"

static QueueHandle_t s_dropbus_rx_queue;
static QueueHandle_t s_chainbus_rx_queue;
static QueueHandle_t s_chainbus_tx_queue;

void bus_queues_init(void)
{
    s_dropbus_rx_queue  = xQueueCreate(BUS_QUEUE_DEPTH, sizeof(spiopen_frame_desc_t));
    s_chainbus_rx_queue = xQueueCreate(BUS_QUEUE_DEPTH, sizeof(spiopen_frame_desc_t));
    s_chainbus_tx_queue = xQueueCreate(BUS_QUEUE_DEPTH, sizeof(spiopen_frame_desc_t));
    configASSERT(s_dropbus_rx_queue != NULL && s_chainbus_rx_queue != NULL && s_chainbus_tx_queue != NULL);
}

void send_to_dropbus_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    xQueueSendFromISR(s_dropbus_rx_queue, &d, pxHigherPriorityTaskWoken);
}

BaseType_t send_to_dropbus_rx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    return xQueueSend(s_dropbus_rx_queue, &d, xTicksToWait);
}

BaseType_t receive_from_dropbus_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait)
{
    return xQueueReceive(s_dropbus_rx_queue, desc, xTicksToWait);
}

void send_to_chainbus_rx_from_isr(uint8_t *buf, uint8_t len, BaseType_t *pxHigherPriorityTaskWoken)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    xQueueSendFromISR(s_chainbus_rx_queue, &d, pxHigherPriorityTaskWoken);
}

BaseType_t receive_from_chainbus_rx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait)
{
    return xQueueReceive(s_chainbus_rx_queue, desc, xTicksToWait);
}

BaseType_t send_to_chainbus_tx(uint8_t *buf, uint8_t len, TickType_t xTicksToWait)
{
    spiopen_frame_desc_t d = { .buf = buf, .len = len };
    return xQueueSend(s_chainbus_tx_queue, &d, xTicksToWait);
}

BaseType_t receive_from_chainbus_tx(spiopen_frame_desc_t *desc, TickType_t xTicksToWait)
{
    return xQueueReceive(s_chainbus_tx_queue, desc, xTicksToWait);
}
