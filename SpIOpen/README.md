# SpIOpen

Protocol for tunneling CAN frames over SPI Bus, specifically focused on CANOpen usage on industrial IO backplanes.

## Bus Layout

Unlike CAN, SpIOpen is primarily meant to act in a master/slave configuration as an industrial fieldbus.
The master node transmits SpIOpen messages (frames) on a common drop-bus (one-to-many) to all slaves simultaneously.
The slave nodes transmit their response SpIOpen messages (frames) on a daisy-chain bus (one-to-one) that propogates all messages towards the master.
This allows fast time synchronization (SYNC messages from master) and physical position detection (TTL signal decremented on chain bus) of slaves while maintaining high data rates by avoiding electronic arbitration.

## Frame Definition

See @SpIOpen_Frame_Definition.md

## Signals

The SpIOpen protocol uses a subset of standard SPI signals, making it a unidirectional protocol:
* Clock Signal (CLK) : Signals data validity on transition
* Data Signal (MOSI) : Presents high or low signal at the time of clock transition

Notes:
* Without a chip select signal, bit-slip can occur. Slaves should be configured to reset all receive ports after the SPI bus has been idle for a certain amount of time.
  * Question: how should this timeout be configured?

# Contents

