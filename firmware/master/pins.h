/**
 * SpIOpen master pin assignment – Waveshare ESP32-C3 Zero.
 * See README.md for wiring and constraints.
 */
#ifndef SPIOPEN_MASTER_PINS_H
#define SPIOPEN_MASTER_PINS_H

/* Drop bus TX: SPI master (MOSI + CLK) to all slaves */
#define SPIOPEN_DROP_TX_MOSI_GPIO   3
#define SPIOPEN_DROP_TX_CLK_GPIO   18

/* Chain bus RX: I2S slave (BCLK + DIN); no CS. WS driven locally if chain has no WS. */
#define SPIOPEN_CHAIN_RX_CLK_GPIO  0   /* I2S BCLK input */
#define SPIOPEN_CHAIN_RX_MOSI_GPIO 1   /* I2S DIN input */
/** WS (word select): chain does not provide it. Use a free GPIO as output (e.g. low) for single-slot stream. */
#define SPIOPEN_CHAIN_RX_WS_GPIO   8

#endif /* SPIOPEN_MASTER_PINS_H */
