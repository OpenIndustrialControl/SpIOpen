/**
 * SpIOpen slave – chainbus RX (upstream chain input).
 * PIO SPI slave (CLK=28, MOSI=29) syncs on preamble 0xAA and pushes bytes to RX FIFO.
 * Two-phase DMA: (1) 5 words (header) into header stage; (2) (frame_len - 5) words into body stage.
 * Unified frame format: preamble, TTL, CID+flags (2), DLC = 5 bytes always.
 */
#include "chainbus_rx.h"
#include "bus_rx_pio.h"
#include "bus_queues.h"
#include "dma_irq.h"
#include "frame_pool.h"
#include "spiopen_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <stddef.h>

#define CHAINBUS_RX_HEADER_LEN    SPIOPEN_HEADER_LEN   /* 5: preamble, TTL, CID+flags (2), DLC */
#define CHAINBUS_RX_HEADER_WORDS  5
#define CHAINBUS_RX_BODY_STAGE_MAX  (SPIOPEN_FRAME_BUF_SIZE - CHAINBUS_RX_HEADER_LEN)

#define NUM_PIO_SM  4u

#define CHAINBUS_RX_TASK_STACK_SIZE  (configMINIMAL_STACK_SIZE * 3)
#define CHAINBUS_RX_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

static PIO s_pio;
static uint s_sm;
static uint s_dma_ch_header;
static uint s_dma_ch_body;
static SemaphoreHandle_t s_chainbus_rx_done_sem;

static uint8_t *s_chainbus_rx_buf;
static uint8_t s_chainbus_rx_frame_len;

static uint32_t s_chainbus_rx_header_stage[CHAINBUS_RX_HEADER_WORDS];
static uint32_t s_chainbus_rx_body_stage[CHAINBUS_RX_BODY_STAGE_MAX];

/**
 * Compute total frame length from the 5-byte header. DLC at byte 4.
 * Returns 0 if invalid.
 */
static uint8_t chainbus_rx_frame_len_from_header(const uint8_t *buf)
{
    if (buf[0] != SPIOPEN_PREAMBLE)
        return 0;
    uint8_t dlc_byte = buf[4];
    uint8_t dlc_raw;
    if (spiopen_dlc_decode(dlc_byte, &dlc_raw) != 0)
        return 0;
    uint8_t data_len = spiopen_dlc_to_byte_count(dlc_raw);
    if (data_len > SPIOPEN_MAX_PAYLOAD)
        return 0;
    uint32_t len = (uint32_t)CHAINBUS_RX_HEADER_LEN + (uint32_t)data_len + SPIOPEN_CRC_BYTES;
    if (len > SPIOPEN_FRAME_BUF_SIZE)
        return 0;
    return (uint8_t)len;
}

static void chainbus_rx_unpack_header(uint8_t *buf)
{
    for (int i = 0; i < CHAINBUS_RX_HEADER_LEN; i++)
        buf[i] = (uint8_t)(s_chainbus_rx_header_stage[i] & 0xFFu);
}

static void chainbus_rx_unpack_body(uint8_t *buf, uint8_t body_len)
{
    for (uint8_t i = 0; i < body_len; i++)
        buf[CHAINBUS_RX_HEADER_LEN + i] = (uint8_t)(s_chainbus_rx_body_stage[i] & 0xFFu);
}

/** Per-channel callbacks: dispatcher acks; we only do work. */
static void chainbus_rx_header_cb(void)
{
    BaseType_t woken = pdFALSE;
    chainbus_rx_unpack_header(s_chainbus_rx_buf);
    s_chainbus_rx_frame_len = chainbus_rx_frame_len_from_header(s_chainbus_rx_buf);
    if (s_chainbus_rx_frame_len == 0) {
        frame_pool_put(s_chainbus_rx_buf);
        s_chainbus_rx_buf = NULL;
        xSemaphoreGiveFromISR(s_chainbus_rx_done_sem, &woken);
    } else {
        uint32_t body_words = (uint32_t)(s_chainbus_rx_frame_len - CHAINBUS_RX_HEADER_LEN);
        dma_channel_set_read_addr(s_dma_ch_body, (void *)&pio0_hw->rxf[s_sm], false);
        dma_channel_set_write_addr(s_dma_ch_body, s_chainbus_rx_body_stage, false);
        dma_channel_set_trans_count(s_dma_ch_body, body_words, true);
    }
    portYIELD_FROM_ISR(woken);
}

static void chainbus_rx_body_cb(void)
{
    BaseType_t woken = pdFALSE;
    if (s_chainbus_rx_buf != NULL && s_chainbus_rx_frame_len != 0) {
        chainbus_rx_unpack_body(s_chainbus_rx_buf, (uint8_t)(s_chainbus_rx_frame_len - CHAINBUS_RX_HEADER_LEN));
        if (spiopen_crc32_verify_frame(s_chainbus_rx_buf, (size_t)s_chainbus_rx_frame_len))
            send_to_chainbus_rx_from_isr(s_chainbus_rx_buf, s_chainbus_rx_frame_len, &woken);
        else
            frame_pool_put(s_chainbus_rx_buf);
        s_chainbus_rx_buf = NULL;
    }
    xSemaphoreGiveFromISR(s_chainbus_rx_done_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void chainbus_rx_task(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        uint8_t *buf = frame_pool_get();
        if (buf == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        while (pio_sm_is_rx_fifo_empty(s_pio, s_sm))
            taskYIELD();

        s_chainbus_rx_buf = buf;
        dma_channel_set_read_addr(s_dma_ch_header, (void *)&pio0_hw->rxf[s_sm], false);
        dma_channel_set_write_addr(s_dma_ch_header, s_chainbus_rx_header_stage, false);
        dma_channel_set_trans_count(s_dma_ch_header, CHAINBUS_RX_HEADER_WORDS, true);

        (void)xSemaphoreTake(s_chainbus_rx_done_sem, portMAX_DELAY);
    }
}

void chainbus_rx_init(void)
{
    bus_rx_pio_get_chainbus(&s_pio, &s_sm);
    configASSERT(s_pio != NULL);
    configASSERT(s_sm < NUM_PIO_SM);

    s_chainbus_rx_done_sem = xSemaphoreCreateBinary();
    configASSERT(s_chainbus_rx_done_sem != NULL);
    s_chainbus_rx_buf = NULL;
    s_chainbus_rx_frame_len = 0;

    s_dma_ch_header = dma_claim_unused_channel(true);
    s_dma_ch_body = dma_claim_unused_channel(true);
    configASSERT((int)s_dma_ch_header >= 0 && s_dma_ch_header < NUM_DMA_CHANNELS);
    configASSERT((int)s_dma_ch_body >= 0 && s_dma_ch_body < NUM_DMA_CHANNELS);
    configASSERT(s_dma_ch_header != s_dma_ch_body);

    const uint dreq_rx = pio_get_dreq(s_pio, s_sm, false);
    for (int i = 0; i < 2; i++) {
        uint ch = (i == 0) ? s_dma_ch_header : s_dma_ch_body;
        dma_channel_config dc = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(&dc, dreq_rx);
        dma_channel_configure(ch, &dc, NULL, NULL, 0, false);
        SPIOPEN_DMA_CH_SET_IRQ_ENABLED(ch, true);
    }
    spiopen_dma_register_channel_callback(s_dma_ch_header, chainbus_rx_header_cb);
    spiopen_dma_register_channel_callback(s_dma_ch_body, chainbus_rx_body_cb);

    BaseType_t task_ok = xTaskCreate(chainbus_rx_task, "chainbus_rx", CHAINBUS_RX_TASK_STACK_SIZE, NULL, CHAINBUS_RX_TASK_PRIORITY, NULL);
    configASSERT(task_ok == pdPASS);
}
