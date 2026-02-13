/**
 * SpIOpen slave – drop bus RX (downstream MOSI drop bus).
 * PIO SPI slave (CLK=26, MOSI=27) syncs on preamble 0xAA and pushes bytes to RX FIFO.
 * Two-phase DMA: (1) 5 words (header) into header stage; (2) (frame_len - 5) into body stage.
 * Unified frame format: preamble, TTL, CID+flags (2), DLC = 5 bytes always.
 */
#include "dropbus_rx.h"
#include "bus_rx_pio.h"
#include "bus_queues.h"
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

#define DROPBUS_RX_HEADER_LEN    SPIOPEN_HEADER_LEN   /* 5: preamble, TTL, CID+flags (2), DLC */
#define DROPBUS_RX_HEADER_WORDS  5
#define DROPBUS_RX_BODY_STAGE_MAX  (SPIOPEN_FRAME_BUF_SIZE - DROPBUS_RX_HEADER_LEN)

#define DROPBUS_RX_TASK_STACK_SIZE  (configMINIMAL_STACK_SIZE * 3)
#define DROPBUS_RX_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

static PIO s_pio;
static uint s_sm;
static uint s_dma_ch_header;
static uint s_dma_ch_body;
static SemaphoreHandle_t s_dropbus_done_sem;

/** Current frame buffer and length (set by task / header IRQ, used in body IRQ). */
static uint8_t *s_dropbus_buf;
static uint8_t s_dropbus_frame_len;

/** Header phase: one 32-bit word per byte (low 8 bits). */
static uint32_t s_dropbus_header_stage[DROPBUS_RX_HEADER_WORDS];
/** Body phase: one word per byte; unpack into buffer after header. */
static uint32_t s_dropbus_body_stage[DROPBUS_RX_BODY_STAGE_MAX];

/**
 * Compute total frame length from the 5-byte header. DLC at byte 4.
 * Returns 0 if invalid.
 */
static uint8_t dropbus_frame_len_from_header(const uint8_t *buf)
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
    uint32_t len = (uint32_t)DROPBUS_RX_HEADER_LEN + (uint32_t)data_len + SPIOPEN_CRC_BYTES;
    if (len > SPIOPEN_FRAME_BUF_SIZE)
        return 0;
    return (uint8_t)len;
}

/** Unpack header words (low 8 bits each) into buf. */
static void dropbus_unpack_header(uint8_t *buf)
{
    for (int i = 0; i < DROPBUS_RX_HEADER_LEN; i++)
        buf[i] = (uint8_t)(s_dropbus_header_stage[i] & 0xFFu);
}

/** Unpack body into buf[HEADER_LEN .. HEADER_LEN+body_len-1]. */
static void dropbus_unpack_body(uint8_t *buf, uint8_t body_len)
{
    for (uint8_t i = 0; i < body_len; i++)
        buf[DROPBUS_RX_HEADER_LEN + i] = (uint8_t)(s_dropbus_body_stage[i] & 0xFFu);
}

/** Acknowledge and check one channel (IRQ0 for ch 0-3, IRQ1 for 4-7). */
#define DROPBUS_DMA_IRQ_STATUS(ch) \
    ((ch) <= 3u ? dma_channel_get_irq0_status(ch) : dma_channel_get_irq1_status(ch))
#define DROPBUS_DMA_IRQ_ACK(ch) \
    do { if ((ch) <= 3u) dma_channel_acknowledge_irq0(ch); else dma_channel_acknowledge_irq1(ch); } while (0)

/**
 * DMA completion: header done -> unpack, parse length, start body DMA;
 * body done -> unpack, verify CRC (software), enqueue or drop, give semaphore.
 */
static void dropbus_rx_dma_irq_handler(void)
{
    BaseType_t woken = pdFALSE;

    if (DROPBUS_DMA_IRQ_STATUS(s_dma_ch_header)) {
        DROPBUS_DMA_IRQ_ACK(s_dma_ch_header);
        dropbus_unpack_header(s_dropbus_buf);
        s_dropbus_frame_len = dropbus_frame_len_from_header(s_dropbus_buf);
        if (s_dropbus_frame_len == 0) {
            frame_pool_put(s_dropbus_buf);
            s_dropbus_buf = NULL;
            xSemaphoreGiveFromISR(s_dropbus_done_sem, &woken);
        } else {
            uint32_t body_words = (uint32_t)(s_dropbus_frame_len - DROPBUS_RX_HEADER_LEN);
            dma_channel_set_read_addr(s_dma_ch_body, (void *)&pio0_hw->rxf[s_sm], false);
            dma_channel_set_write_addr(s_dma_ch_body, s_dropbus_body_stage, false);
            dma_channel_set_trans_count(s_dma_ch_body, body_words, true);
        }
    }

    if (DROPBUS_DMA_IRQ_STATUS(s_dma_ch_body)) {
        DROPBUS_DMA_IRQ_ACK(s_dma_ch_body);
        if (s_dropbus_buf != NULL && s_dropbus_frame_len != 0) {
            dropbus_unpack_body(s_dropbus_buf, (uint8_t)(s_dropbus_frame_len - DROPBUS_RX_HEADER_LEN));
            if (spiopen_crc32_verify_frame(s_dropbus_buf, (size_t)s_dropbus_frame_len))
                send_to_dropbus_rx_from_isr(s_dropbus_buf, s_dropbus_frame_len, &woken);
            else
                frame_pool_put(s_dropbus_buf);
            s_dropbus_buf = NULL;
        }
        xSemaphoreGiveFromISR(s_dropbus_done_sem, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

static void dropbus_rx_task(void *pvParameters)
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

        s_dropbus_buf = buf;
        dma_channel_set_read_addr(s_dma_ch_header, (void *)&pio0_hw->rxf[s_sm], false);
        dma_channel_set_write_addr(s_dma_ch_header, s_dropbus_header_stage, false);
        dma_channel_set_trans_count(s_dma_ch_header, DROPBUS_RX_HEADER_WORDS, true);

        (void)xSemaphoreTake(s_dropbus_done_sem, portMAX_DELAY);
    }
}

void dropbus_rx_init(void)
{
    bus_rx_pio_get_dropbus(&s_pio, &s_sm);

    s_dma_ch_header = dma_claim_unused_channel(true);
    s_dma_ch_body = dma_claim_unused_channel(true);

    const uint dreq_rx = pio_get_dreq(s_pio, s_sm, false);
    for (int i = 0; i < 2; i++) {
        uint ch = (i == 0) ? s_dma_ch_header : s_dma_ch_body;
        dma_channel_config dc = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(&dc, dreq_rx);
        dma_channel_configure(ch, &dc, NULL, NULL, 0, false);
        if (ch <= 3u)
            dma_channel_set_irq0_enabled(ch, true);
        else
            dma_channel_set_irq1_enabled(ch, true);
    }
    irq_add_shared_handler(DMA_IRQ_0, dropbus_rx_dma_irq_handler, 0);
    irq_set_enabled(DMA_IRQ_0, true);
    if (s_dma_ch_header > 3u || s_dma_ch_body > 3u) {
        irq_add_shared_handler(DMA_IRQ_1, dropbus_rx_dma_irq_handler, 0);
        irq_set_enabled(DMA_IRQ_1, true);
    }

    s_dropbus_done_sem = xSemaphoreCreateBinary();
    configASSERT(s_dropbus_done_sem != NULL);
    s_dropbus_buf = NULL;
    s_dropbus_frame_len = 0;

    xTaskCreate(dropbus_rx_task, "dropbus_rx", DROPBUS_RX_TASK_STACK_SIZE, NULL, DROPBUS_RX_TASK_PRIORITY, NULL);
}
