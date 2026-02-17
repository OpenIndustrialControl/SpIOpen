/**
 * SpIOpen slave – chainbus TX over hardware SPI with DMA.
 *
 * SPI0 master: CLK=GPIO2, MOSI=GPIO3 @ 10 MHz (per README / DevelopmentPlan).
 *
 * Design:
 * - A single FreeRTOS task blocks on chainbus_tx_queue. When a frame descriptor
 *   (buf, len) appears, the task programs one DMA channel to copy bytes from
 *   buf into the SPI TX FIFO, then blocks on a binary semaphore.
 * - The DMA engine is paced by DREQ_SPI0_TX: it only pushes a byte when the
 *   SPI FIFO has space, so we don't overrun. When the transfer count reaches
 *   zero, the RP2040 raises a DMA completion interrupt.
 * - The DMA IRQ handler returns the buffer to the frame pool, then "gives"
 *   the semaphore. The task wakes, loops back, and either starts the next
 *   transfer or blocks again on the queue. The semaphore therefore means
 *   "the current DMA transfer has finished."
 *
 * The single RP2040 DMA sniffer (CRC-32) is used on this channel only: we have
 * the full frame in one buffer and one DMA, so hardware CRC fits here. Other
 * ports (dropbus RX, etc.) use software spiopen_crc32.
 *
 * RP2040 DMA interrupt rule: channels 0–3 fire DMA_IRQ_0; channels 4–7 fire
 * DMA_IRQ_1. We use one channel, so we enable/ack the correct IRQ by channel.
 */
#include "chainbus_tx_spi.h"
#include "dma_irq.h"
#include "bus_queues.h"
#include "frame_pool.h"
#include "spiopen_protocol.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/* Chainbus output pins per README / DevelopmentPlan */
#define CHAINBUS_TX_SPI_CLK_PIN   2
#define CHAINBUS_TX_SPI_MOSI_PIN  3
#define CHAINBUS_TX_SPI_BAUD_HZ   (10u * 1000)   /* 10 kHz for Phase 1 test */

#define TX_TASK_STACK_SIZE     (configMINIMAL_STACK_SIZE * 2)
#define TX_TASK_PRIORITY       (tskIDLE_PRIORITY + 2)

static spi_inst_t *const s_spi = spi0;
static uint s_dma_ch;
static SemaphoreHandle_t s_tx_done_sem;
static volatile uint8_t *s_current_tx_buf;

/** Per-channel callback: dispatcher already confirmed this channel fired; we do work only (dispatcher acks). */
static void chainbus_tx_dma_cb(void)
{
    BaseType_t woken = pdFALSE;
    (void)dma_sniffer_get_data_accumulator();
    dma_sniffer_disable();
    if (s_current_tx_buf != NULL) {
        frame_pool_put((uint8_t *)s_current_tx_buf);
        s_current_tx_buf = NULL;
    }
    xSemaphoreGiveFromISR(s_tx_done_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void chainbus_tx_task(void *pvParameters)
{
    spiopen_frame_desc_t desc;
    (void)pvParameters;

    for (;;) {
        if (receive_from_chainbus_tx(&desc, portMAX_DELAY) != pdTRUE)
            continue;

        s_current_tx_buf = desc.buf;
        /* Send preamble before DMA; buffer holds [TTL, CID, DLC, data, CRC] only. */
        {
            const uint8_t preamble_byte = (uint8_t)SPIOPEN_PREAMBLE;
            spi_write_blocking(s_spi, &preamble_byte, 1);
        }

        dma_channel_set_read_addr(s_dma_ch, desc.buf, false);
        dma_channel_set_trans_count(s_dma_ch, (uint32_t)desc.len, false);

        /* DMA sniffer (global) computes CRC-32 on this channel (buffer only; preamble excluded). */
        dma_sniffer_set_data_accumulator(0xFFFFFFFFu);
        dma_sniffer_enable(s_dma_ch, 0, true);  /* mode 0 = CRC-32 IEEE 802.3 */

        dma_channel_start(s_dma_ch);

        (void)xSemaphoreTake(s_tx_done_sem, portMAX_DELAY);
    }
}

void chainbus_tx_spi_init(void)
{
    configASSERT(s_spi != NULL);
    configASSERT(CHAINBUS_TX_SPI_BAUD_HZ > 0u);
    configASSERT(TX_TASK_STACK_SIZE > 0u);

    spi_init(s_spi, CHAINBUS_TX_SPI_BAUD_HZ);
    gpio_set_function(CHAINBUS_TX_SPI_CLK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CHAINBUS_TX_SPI_MOSI_PIN, GPIO_FUNC_SPI);
    spi_set_format(s_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    s_dma_ch = dma_claim_unused_channel(true);
    configASSERT((int)s_dma_ch >= 0 && s_dma_ch < NUM_DMA_CHANNELS);

    {
        dma_channel_config dc = dma_channel_get_default_config(s_dma_ch);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, spi_get_dreq(s_spi, true));
        dma_channel_configure(s_dma_ch, &dc,
            (void *)&spi_get_hw(s_spi)->dr,
            NULL, 0, false);
    }

    spiopen_dma_register_channel_callback(s_dma_ch, chainbus_tx_dma_cb);
    SPIOPEN_DMA_CH_SET_IRQ_ENABLED(s_dma_ch, true);

    s_tx_done_sem = xSemaphoreCreateBinary();
    configASSERT(s_tx_done_sem != NULL);
    s_current_tx_buf = NULL;

    BaseType_t task_ok = xTaskCreate(chainbus_tx_task, "chainbus_tx", TX_TASK_STACK_SIZE, NULL, TX_TASK_PRIORITY, NULL);
    configASSERT(task_ok == pdPASS);
}
