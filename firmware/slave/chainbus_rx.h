/**
 * SpIOpen slave – chainbus RX (upstream chain input).
 * PIO SPI slave (CLK=28, MOSI=29) syncs on preamble 0xAA and pushes bytes to
 * its RX FIFO. Two-phase DMA moves bytes into a pool buffer; when a full
 * chain-format frame is received and CRC is valid, (buf, len) is enqueued to
 * chainbus_rx_queue for the TTL task (decrement TTL, forward to chainbus TX).
 */
#ifndef CHAINBUS_RX_H
#define CHAINBUS_RX_H

/**
 * Initialize PIO (preamble sync + byte push), two DMA channels, and chainbus RX task.
 * Call after bus_queues_init(). Frames are delivered to chainbus_rx_queue for TTL/forward.
 */
void chainbus_rx_init(void);

#endif /* CHAINBUS_RX_H */
