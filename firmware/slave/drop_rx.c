/**
 * SpIOpen slave – drop bus RX.
 * PIO SPI slave (CLK=26, MOSI=27) syncs on preamble 0xAA and pushes bytes to RX FIFO.
 * Two-phase DMA: (1) 6 words from PIO RX FIFO (one byte per word) into header stage,
 * unpack to buffer; (2) (frame_len - 6) words into body stage, unpack to buffer+6.
 * On full frame, verify CRC in software (spiopen_crc32_verify_frame); if valid,
 * enqueue (buf, len) to drop_rx_queue from the IRQ. Otherwise return buffer to pool.
 */
#include "drop_rx.h"
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

/* Pins per README / DevelopmentPlan */
#define DROP_RX_CLK_GPIO   26
#define DROP_RX_MOSI_GPIO  27

#define DROP_RX_HEADER_LEN  6
/* PIO pushes 8 bits at a time; each push is one 32-bit FIFO word (byte in low 8 bits). */
#define DROP_RX_HEADER_WORDS  6
#define DROP_RX_BODY_STAGE_MAX  (SPIOPEN_FRAME_BUF_SIZE - DROP_RX_HEADER_LEN)

#define DROP_RX_TASK_STACK_SIZE  (configMINIMAL_STACK_SIZE * 3)
#define DROP_RX_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

#include "drop_rx_pio.pio.h"

static PIO s_pio;
static uint s_sm;
static uint s_sm_offset;
static uint s_dma_ch_header;
static uint s_dma_ch_body;
static SemaphoreHandle_t s_drop_done_sem;

/** Current frame buffer and length (set by task / header IRQ, used in body IRQ). */
static uint8_t *s_drop_buf;
static uint8_t s_drop_frame_len;

/** Header phase: one 32-bit word per byte (low 8 bits). */
static uint32_t s_drop_header_stage[DROP_RX_HEADER_WORDS];
/** Body phase: one word per byte; unpack low 8 bits into buffer+6. */
static uint32_t s_drop_body_stage[DROP_RX_BODY_STAGE_MAX];

/**
 * Compute total drop frame length from the first 6 bytes (MOSI Drop Bus format).
 * Returns 0 if invalid.
 */
static uint8_t drop_frame_len_from_header(const uint8_t *buf)
{
    if (buf[0] != SPIOPEN_PREAMBLE)
        return 0;
    uint8_t header_len = (buf[1] & 1u) ? 6u : 4u;
    uint8_t dlc_byte = buf[header_len - 1u];
    uint8_t dlc_raw;
    if (spiopen_dlc_decode(dlc_byte, &dlc_raw) != 0)
        return 0;
    uint8_t data_len = spiopen_dlc_to_byte_count(dlc_raw);
    if (data_len > SPIOPEN_MAX_PAYLOAD)
        return 0;
    uint32_t len = (uint32_t)header_len + (uint32_t)data_len + SPIOPEN_CRC_BYTES;
    if (len > SPIOPEN_FRAME_BUF_SIZE)
        return 0;
    return (uint8_t)len;
}

/** Unpack 6 words: each word has one byte in the low 8 bits. */
static void drop_unpack_header(uint8_t *buf)
{
    for (int i = 0; i < DROP_RX_HEADER_LEN; i++)
        buf[i] = (uint8_t)(s_drop_header_stage[i] & 0xFFu);
}

/** Unpack body_words into buf[6 .. 6+body_len-1]. */
static void drop_unpack_body(uint8_t *buf, uint8_t body_len)
{
    for (uint8_t i = 0; i < body_len; i++)
        buf[DROP_RX_HEADER_LEN + i] = (uint8_t)(s_drop_body_stage[i] & 0xFFu);
}

/** Acknowledge and check one channel (IRQ0 for ch 0-3, IRQ1 for 4-7). */
#define DROP_DMA_IRQ_STATUS(ch) \
    ((ch) <= 3u ? dma_channel_get_irq0_status(ch) : dma_channel_get_irq1_status(ch))
#define DROP_DMA_IRQ_ACK(ch) \
    do { if ((ch) <= 3u) dma_channel_acknowledge_irq0(ch); else dma_channel_acknowledge_irq1(ch); } while (0)

/**
 * DMA completion: header done -> unpack, parse length, start body DMA;
 * body done -> unpack, verify CRC (software), enqueue or drop, give semaphore.
 */
static void drop_rx_dma_irq_handler(void)
{
    BaseType_t woken = pdFALSE;

    if (DROP_DMA_IRQ_STATUS(s_dma_ch_header)) {
        DROP_DMA_IRQ_ACK(s_dma_ch_header);
        drop_unpack_header(s_drop_buf);
        s_drop_frame_len = drop_frame_len_from_header(s_drop_buf);
        if (s_drop_frame_len == 0) {
            frame_pool_put(s_drop_buf);
            s_drop_buf = NULL;
            xSemaphoreGiveFromISR(s_drop_done_sem, &woken);
        } else {
            uint32_t body_words = (uint32_t)(s_drop_frame_len - DROP_RX_HEADER_LEN);
            dma_channel_set_read_addr(s_dma_ch_body, (void *)&pio0_hw->rxf[s_sm], false);
            dma_channel_set_write_addr(s_dma_ch_body, s_drop_body_stage, false);
            dma_channel_set_trans_count(s_dma_ch_body, body_words, true);
        }
    }

    if (DROP_DMA_IRQ_STATUS(s_dma_ch_body)) {
        DROP_DMA_IRQ_ACK(s_dma_ch_body);
        if (s_drop_buf != NULL && s_drop_frame_len != 0) {
            drop_unpack_body(s_drop_buf, (uint8_t)(s_drop_frame_len - DROP_RX_HEADER_LEN));
            if (spiopen_crc32_verify_frame(s_drop_buf, (size_t)s_drop_frame_len))
                send_to_drop_rx_from_isr(s_drop_buf, s_drop_frame_len, &woken);
            else
                frame_pool_put(s_drop_buf);
            s_drop_buf = NULL;
        }
        xSemaphoreGiveFromISR(s_drop_done_sem, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

static void drop_rx_task(void *pvParameters)
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

        s_drop_buf = buf;
        dma_channel_set_read_addr(s_dma_ch_header, (void *)&pio0_hw->rxf[s_sm], false);
        dma_channel_set_write_addr(s_dma_ch_header, s_drop_header_stage, false);
        dma_channel_set_trans_count(s_dma_ch_header, DROP_RX_HEADER_WORDS, true);

        (void)xSemaphoreTake(s_drop_done_sem, portMAX_DELAY);
    }
}

void drop_rx_init(void)
{
    s_pio = pio0;
    s_sm_offset = pio_add_program(s_pio, &drop_rx_spi_slave_program);
    s_sm = pio_claim_unused_sm(s_pio, true);

    pio_sm_config c = drop_rx_spi_slave_program_get_default_config(s_sm_offset);
    sm_config_set_in_pins(&c, DROP_RX_MOSI_GPIO);
    sm_config_set_set_pins(&c, DROP_RX_CLK_GPIO, 1);
    sm_config_set_in_shift(&c, true, true, 8);
    sm_config_set_out_shift(&c, true, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, 1.f);

    pio_sm_set_pindirs_with_mask(s_pio, s_sm, 0u, (1u << DROP_RX_CLK_GPIO) | (1u << DROP_RX_MOSI_GPIO));
    pio_gpio_init(s_pio, DROP_RX_CLK_GPIO);
    pio_gpio_init(s_pio, DROP_RX_MOSI_GPIO);
    pio_sm_init(s_pio, s_sm, s_sm_offset, &c);
    pio_sm_put_blocking(s_pio, s_sm, (uint32_t)SPIOPEN_PREAMBLE);  /* PIO pull loads 0xAA for sync */
    pio_sm_set_enabled(s_pio, s_sm, true);

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
    irq_add_shared_handler(DMA_IRQ_0, drop_rx_dma_irq_handler, 0);
    irq_set_enabled(DMA_IRQ_0, true);
    if (s_dma_ch_header > 3u || s_dma_ch_body > 3u) {
        irq_add_shared_handler(DMA_IRQ_1, drop_rx_dma_irq_handler, 0);
        irq_set_enabled(DMA_IRQ_1, true);
    }

    s_drop_done_sem = xSemaphoreCreateBinary();
    configASSERT(s_drop_done_sem != NULL);
    s_drop_buf = NULL;
    s_drop_frame_len = 0;

    xTaskCreate(drop_rx_task, "drop_rx", DROP_RX_TASK_STACK_SIZE, NULL, DROP_RX_TASK_PRIORITY, NULL);
}
