/**
 * SpIOpen master – chain bus RX (SPI slave input: CLK + MOSI).
 * Receives frames from upstream slave; pushes to SpIOpen RX queue.
 */
#ifndef SPIOPEN_RX_SLAVE_H
#define SPIOPEN_RX_SLAVE_H

void spiopen_rx_slave_init(void);

#endif /* SPIOPEN_RX_SLAVE_H */
