/**
 * SpIOpen slave – chain output over hardware SPI (master mode) with DMA.
 * Pins: CLK=GPIO2, MOSI=GPIO3. Target 10 MHz.
 */
#ifndef CHAIN_TX_SPI_H
#define CHAIN_TX_SPI_H

/**
 * Initialize SPI (GPIO2/3, 10 MHz), DMA channel, and the TX task.
 * Call after bus_queues_init(). The task drains chain_tx_queue and
 * sends frames via SPI DMA; on completion returns buffer to frame pool.
 */
void chain_tx_spi_init(void);

#endif /* CHAIN_TX_SPI_H */
