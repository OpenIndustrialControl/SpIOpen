/**
 * SpIOpen slave – dropbus RX (downstream MOSI drop bus).
 * PIO SPI slave (CLK=26, MOSI=27) syncs on preamble 0xAA and pushes bytes to
 * its RX FIFO. Two-phase DMA moves bytes from the FIFO into a pool buffer;
 * when a full frame is received, (buf, len) is enqueued to dropbus_rx_queue.
 */
#ifndef DROPBUS_RX_H
#define DROPBUS_RX_H

/**
 * Initialize PIO (preamble sync + byte push), two DMA channels, and dropbus RX task.
 * Call after bus_queues_init(). Frames are delivered to dropbus_rx_queue for the app.
 */
void dropbus_rx_init(void);

#endif /* DROPBUS_RX_H */
