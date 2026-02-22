/**
 * CANopenNode driver over SpIOpen (ESP32-C3 master).
 * TX: CO_CANsend enqueues to SpIOpen TX (drop bus). RX: chain bus frames
 * are injected by spiopen_can_driver_inject_rx() from the CANopen task.
 */
#include "301/CO_driver.h"
#include "spiopen_can_driver.h"
#include "spiopen_canopen.h"
#include "frame_pool.h"
#include "spiopen_queues.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stddef.h>

static SemaphoreHandle_t s_can_send_mutex;
static CO_CANmodule_t *s_CANmodule;

void CO_lock_can_send(void)
{
    if (s_can_send_mutex != NULL)
        (void)xSemaphoreTake(s_can_send_mutex, portMAX_DELAY);
}

void CO_unlock_can_send(void)
{
    if (s_can_send_mutex != NULL)
        (void)xSemaphoreGive(s_can_send_mutex);
}

void CO_CANsetConfigurationMode(void *CANptr)
{
    (void)CANptr;
}

void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule)
{
    if (CANmodule != NULL)
        CANmodule->CANnormal = true;
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t *CANmodule, void *CANptr, CO_CANrx_t rxArray[],
    uint16_t rxSize, CO_CANtx_t txArray[], uint16_t txSize, uint16_t CANbitRate)
{
    (void)CANbitRate;
    if (CANmodule == NULL || rxArray == NULL || txArray == NULL)
        return CO_ERROR_ILLEGAL_ARGUMENT;
    s_CANmodule = CANmodule;

    CANmodule->CANptr = CANptr;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0;
    CANmodule->CANnormal = false;
    CANmodule->useCANrxFilters = false;
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0;

    for (uint16_t i = 0; i < rxSize; i++) {
        rxArray[i].ident = 0;
        rxArray[i].mask = 0xFFFFu;
        rxArray[i].object = NULL;
        rxArray[i].CANrx_callback = NULL;
    }
    for (uint16_t i = 0; i < txSize; i++) {
        txArray[i].bufferFull = false;
    }
    return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t *CANmodule)
{
    (void)CANmodule;
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index, uint16_t ident,
    uint16_t mask, bool_t rtr, void *object, void (*CANrx_callback)(void *object, void *message))
{
    if (CANmodule == NULL || object == NULL || CANrx_callback == NULL || index >= CANmodule->rxSize)
        return CO_ERROR_ILLEGAL_ARGUMENT;
    CO_CANrx_t *buffer = &CANmodule->rxArray[index];
    buffer->object = object;
    buffer->CANrx_callback = CANrx_callback;
    buffer->ident = ident & 0x07FFu;
    if (rtr)
        buffer->ident |= 0x0800u;
    buffer->mask = (mask & 0x07FFu) | 0x0800u;
    return CO_ERROR_NO;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index, uint16_t ident,
    bool_t rtr, uint8_t noOfBytes, bool_t syncFlag)
{
    if (CANmodule == NULL || index >= CANmodule->txSize)
        return NULL;
    CO_CANtx_t *buffer = &CANmodule->txArray[index];
    buffer->ident = ((uint32_t)(ident & 0x07FFu))
        | ((uint32_t)((uint32_t)(noOfBytes & 0xFu) << 11u))
        | ((uint32_t)(rtr ? 0x8000u : 0u));
    buffer->DLC = noOfBytes;
    buffer->bufferFull = false;
    buffer->syncFlag = syncFlag;
    return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
    if (buffer->bufferFull) {
        if (!CANmodule->firstCANtxMessage)
            CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
        return CO_ERROR_TX_OVERFLOW;
    }

    CO_LOCK_CAN_SEND(CANmodule);

    uint8_t *tx_buf = frame_pool_get();
    if (tx_buf != NULL) {
        size_t total = spiopen_frame_from_canopen_tx((uint16_t)(buffer->ident & 0x07FFu),
            buffer->DLC, buffer->data, tx_buf, (size_t)SPIOPEN_FRAME_BUF_SIZE,
            (uint8_t)SPIOPEN_CAN_TTL_DEFAULT);
        if (total != 0) {
            if (send_to_spiopen_tx(tx_buf, (uint8_t)total, 0) == pdTRUE) {
                buffer->bufferFull = false;
                CANmodule->firstCANtxMessage = false;
            } else {
                frame_pool_put(tx_buf);
                buffer->bufferFull = true;
                CANmodule->CANtxCount++;
            }
        } else {
            frame_pool_put(tx_buf);
            buffer->bufferFull = true;
            CANmodule->CANtxCount++;
        }
    } else {
        buffer->bufferFull = true;
        CANmodule->CANtxCount++;
    }

    CO_UNLOCK_CAN_SEND(CANmodule);
    return CO_ERROR_NO;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t *CANmodule)
{
    CO_LOCK_CAN_SEND(CANmodule);
    if (CANmodule->bufferInhibitFlag)
        CANmodule->bufferInhibitFlag = false;
    for (uint16_t i = 0; i < CANmodule->txSize; i++) {
        CO_CANtx_t *b = &CANmodule->txArray[i];
        if (b->bufferFull && b->syncFlag) {
            b->bufferFull = false;
            if (CANmodule->CANtxCount > 0)
                CANmodule->CANtxCount--;
        }
    }
    CO_UNLOCK_CAN_SEND(CANmodule);
}

void CO_CANmodule_process(CO_CANmodule_t *CANmodule)
{
    (void)CANmodule;
}

void spiopen_can_driver_init(void)
{
    if (s_can_send_mutex == NULL)
        s_can_send_mutex = xSemaphoreCreateMutex();
}

void *spiopen_can_driver_get_CANmodule(void)
{
    return (void *)s_CANmodule;
}

static CO_CANrxMsg_t s_rx_msg;

int spiopen_can_driver_inject_rx(uint8_t *buf, size_t len)
{
    if (s_CANmodule == NULL || buf == NULL)
        return -1;
    if (spiopen_frame_to_canopen_rx(buf, len, &s_rx_msg) == 0)
        return -1;

    frame_pool_put(buf);

    uint16_t ident = s_rx_msg.ident;
    CO_CANrx_t *buffer = &s_CANmodule->rxArray[0];
    for (uint16_t i = s_CANmodule->rxSize; i > 0; i--) {
        if (((ident ^ buffer->ident) & buffer->mask) == 0u
            && buffer->object != NULL
            && buffer->CANrx_callback != NULL) {
            buffer->CANrx_callback(buffer->object, (void *)&s_rx_msg);
            break;
        }
        buffer++;
    }
    return 0;
}
