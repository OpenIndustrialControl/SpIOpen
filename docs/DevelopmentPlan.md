# SpIOpen Development Plan

Short summary of the phased approach. Full architecture: [Architecture.md](Architecture.md).

## Repository layout

| Path | Description |
|------|-------------|
| [lib/spiopen_protocol/](../lib/spiopen_protocol/) | Shared frame encode/decode, CRC-32, Hamming DLC |
| [firmware/slave/](../firmware/slave/) | RP2040 (Pico SDK) – Phase 1 loopback → full slave + CANopenNode |
| [firmware/master/](../firmware/master/) | ESP32 XIAO master (Phase 3) |
| [docs/](.) | Architecture and this plan |

## Phase 1: Single-board loopback (RP2040 XIAO)

**Goal:** Validate 10 MHz bus and frame handling with one native SPI output and two PIO-based inputs.

**Electrical:**

- **Output:** One native SPI in master mode (CLK + MOSI, DMA). Feeds both inputs in loopback.
- **Input 1 – MOSI Drop Bus (PIO):** CLK + MOSI, same frame format as chain (preamble, TTL, CID+flags, DLC, data, CRC). Mock/empty handler.
- **Input 2 – Chain (PIO):** CLK + MOSI, chain format (with TTL). Decrement TTL, retransmit out same SPI output.

**Pin assignments (RP2040 XIAO, loopback only):**

PIO RX ports use a **MOSI-first** group: pin 0 = MOSI (data), pin 1 = CLK; GPIOs must be consecutive (CLK = MOSI + 1).

| GPIO | Role | Interface |
|------|------|-----------|
| 26 | Downstream drop bus MOSI (data) — PIO pin 0 | PIO |
| 27 | Downstream drop bus CLK — PIO pin 1 | PIO |
| 28 | Upstream chain bus input MOSI (data) — PIO pin 0 | PIO |
| 29 | Upstream chain bus input CLK — PIO pin 1 | PIO |
| 2 | Upstream chain bus output CLK | Hardware SPI |
| 3 | Upstream chain bus output MOSI (data) | Hardware SPI |

**Wiring:** SPI output CLK and MOSI wired to **both** PIO inputs' CLK and MOSI (connect SPI MOSI to GPIO 26 and 28, SPI CLK to GPIO 27 and 29).

**Success criteria:** Chain path echo at 10 MHz with TTL decrement and no CRC errors; drop path receives frames (mock handler); optional LED/UART log.

## Phase 2: Single slave + CANopenNode

Full protocol in `lib/spiopen_protocol`; slave firmware with CANopenNode and custom SpIOpen transport. Test with host or ESP32 master.

## Phase 3: Master + multiple slaves

ESP32 XIAO master in [firmware/master/](../firmware/master/); multiple RP2040 slaves; LSS and TTL on chain.
