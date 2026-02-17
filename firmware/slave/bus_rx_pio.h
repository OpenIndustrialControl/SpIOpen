/**
 * SpIOpen slave – shared PIO for dropbus RX and chainbus RX.
 * One program (spiopen_bus_rx) loaded once; two state machines (SM 0 = dropbus, SM 1 = chainbus).
 * Call bus_rx_pio_init() before dropbus_rx_init() and chainbus_rx_init().
 */
#ifndef BUS_RX_PIO_H
#define BUS_RX_PIO_H

#include "hardware/pio.h"
#include "pico/types.h"

/** Initialize PIO0: load program once, claim and start SM 0 (dropbus) and SM 1 (chainbus). */
void bus_rx_pio_init(void);

/** Get PIO and state machine for dropbus RX (CLK=26, MOSI=27). */
void bus_rx_pio_get_dropbus(PIO *pio, uint *sm);

/** Get PIO and state machine for chainbus RX (CLK=28, MOSI=29). */
void bus_rx_pio_get_chainbus(PIO *pio, uint *sm);

/** Restart the dropbus PIO SM to program start so it re-syncs on preamble. Call from DMA body-complete IRQ. */
void bus_rx_pio_restart_dropbus(void);

/** Restart the chainbus PIO SM to program start so it re-syncs on preamble. Call from DMA body-complete IRQ. */
void bus_rx_pio_restart_chainbus(void);

#endif /* BUS_RX_PIO_H */
