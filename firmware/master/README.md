# SpIOpen master (ESP32 XIAO) – Phase 3

Placeholder for the ESP32 XIAO master firmware. Phase 3 will add:

- One SPI as **master** for MOSI Drop Bus (CLK + MOSI)
- One SPI as **slave** for MISO Chain Bus (CLK + MISO)
- CANopenNode in master mode (NMT, SDO client, LSS)
- Same frame format and `lib/spiopen_protocol` as the slave

See [docs/Architecture.md](../../docs/Architecture.md) and [docs/DevelopmentPlan.md](../../docs/DevelopmentPlan.md).
