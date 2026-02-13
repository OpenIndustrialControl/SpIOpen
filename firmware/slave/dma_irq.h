/**
 * SpIOpen slave – shared DMA IRQ helpers for RP2040.
 * Channels 0–3 use DMA_IRQ_0; channels 4–7 use DMA_IRQ_1.
 */
#ifndef SPIOPEN_SLAVE_DMA_IRQ_H
#define SPIOPEN_SLAVE_DMA_IRQ_H

#include "hardware/dma.h"
#include <stdint.h>

#define SPIOPEN_DMA_IRQ0_CHANNEL_MAX   3u

/** Enable or disable the completion IRQ for the given channel. */
#define SPIOPEN_DMA_CH_SET_IRQ_ENABLED(ch, en) \
    do { \
        if ((ch) <= SPIOPEN_DMA_IRQ0_CHANNEL_MAX) \
            dma_channel_set_irq0_enabled(ch, en); \
        else \
            dma_channel_set_irq1_enabled(ch, en); \
    } while (0)

/**
 * Central dispatcher: only two IRQ handlers are registered (one per line).
 * Call once before any DMA-using module inits.
 */
void spiopen_dma_irq_dispatcher_init(void);

/**
 * Register a callback for the given DMA channel (0–11).
 * When that channel's completion IRQ fires, the dispatcher will call this callback;
 * the dispatcher acks the IRQ after the callback returns.
 */
void spiopen_dma_register_channel_callback(unsigned int channel, void (*callback)(void));

#endif /* SPIOPEN_SLAVE_DMA_IRQ_H */
