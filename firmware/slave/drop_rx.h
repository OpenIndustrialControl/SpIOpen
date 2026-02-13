/**
 * SpIOpen slave – drop bus RX (downstream MOSI drop).
 * PIO SPI slave (CLK=26, MOSI=27) syncs on preamble 0xAA and pushes bytes to
 * its RX FIFO. Two-phase DMA moves bytes from the FIFO into a pool buffer;
 * when a full frame is received, (buf, len) is enqueued to drop_rx_queue.
 */
#ifndef DROP_RX_H
#define DROP_RX_H

/**
 * Initialize PIO (preamble sync + byte push), two DMA channels, and drop RX task.
 * Call after bus_queues_init(). Frames are delivered to drop_rx_queue for the app.
 */
void drop_rx_init(void);

#endif /* DROP_RX_H */
