/**
 * SpIOpen slave – central DMA IRQ dispatcher.
 * Only two handlers are ever registered (DMA_IRQ_0 and DMA_IRQ_1).
 * Modules register per-channel callbacks; when an IRQ fires we dispatch by channel number.
 */
#include "dma_irq.h"
#include "FreeRTOS.h"
#include "hardware/irq.h"
#include <stddef.h>

#define NUM_CHANNELS  NUM_DMA_CHANNELS

typedef void (*dma_channel_cb_t)(void);

static dma_channel_cb_t s_callback[NUM_CHANNELS];

static void dma_irq0_dispatcher(void)
{
    for (unsigned int ch = 0u; ch <= SPIOPEN_DMA_IRQ0_CHANNEL_MAX; ch++) {
        if (dma_channel_get_irq0_status(ch)) {
            if (s_callback[ch] != NULL)
                s_callback[ch]();
            dma_channel_acknowledge_irq0(ch);
        }
    }
}

static void dma_irq1_dispatcher(void)
{
    for (unsigned int ch = SPIOPEN_DMA_IRQ0_CHANNEL_MAX + 1u; ch < NUM_CHANNELS; ch++) {
        if (dma_channel_get_irq1_status(ch)) {
            if (s_callback[ch] != NULL)
                s_callback[ch]();
            dma_channel_acknowledge_irq1(ch);
        }
    }
}

void spiopen_dma_irq_dispatcher_init(void)
{
    for (unsigned int i = 0u; i < NUM_CHANNELS; i++)
        s_callback[i] = NULL;

    irq_add_shared_handler(DMA_IRQ_0, dma_irq0_dispatcher, 0);
    irq_set_enabled(DMA_IRQ_0, true);
    irq_add_shared_handler(DMA_IRQ_1, dma_irq1_dispatcher, 0);
    irq_set_enabled(DMA_IRQ_1, true);
}

void spiopen_dma_register_channel_callback(unsigned int channel, void (*callback)(void))
{
    configASSERT(channel < (unsigned)NUM_CHANNELS);
    s_callback[channel] = callback;
}
