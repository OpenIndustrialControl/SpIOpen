# SpIOpen

High-speed industrial IO backplane using CANopen-style messages over a dual SPI bus (MOSI Drop + MISO Chain). See [System Architecture](docs/Architecture.md) and [Development Plan](docs/DevelopmentPlan.md).

Development is phased: **Phase 1** loopback on RP2040 XIAO (one SPI output, two PIO inputs) → **Phase 2** single slave with full protocol and CANopenNode → **Phase 3** ESP32 XIAO master and multiple slaves.

| Directory | Description |
|----------|-------------|
| [docs/](docs/) | Architecture and development plan |
| [lib/spiopen_protocol/](lib/spiopen_protocol/) | Shared frame format, CRC-32, Hamming DLC |
| [firmware/slave/](firmware/slave/) | RP2040 slave (Pico SDK); Phase 1 loopback target |
| [firmware/master/](firmware/master/) | ESP32 XIAO master (Phase 3 placeholder) |