/**
 * SpIOpen slave – chainbus output over hardware SPI (master mode) with DMA.
 * Pins: CLK=GPIO2, MOSI=GPIO3. Target 10 MHz.
 */
#ifndef CHAINBUS_TX_SPI_H
#define CHAINBUS_TX_SPI_H

/**
 * Initialize SPI (GPIO2/3, 10 MHz), DMA channel, and the TX task.
 * Call after bus_queues_init(). The task drains chainbus_tx_queue and
 * sends frames via SPI DMA; on completion returns buffer to frame pool.
 */
void chainbus_tx_spi_init(void);

#endif /* CHAINBUS_TX_SPI_H */
