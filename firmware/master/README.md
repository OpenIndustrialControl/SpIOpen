# SpIOpen CANopen Master (ESP32-C3 Zero) – Phase 2

CANopenNode in **master** mode on the Waveshare ESP32-C3 Zero. Host computer talks over **USB serial** using the CANopen ASCII gateway (CiA 309-3). SpIOpen transport: one SPI **master** for drop-bus TX, one SPI **slave** for chain-bus RX.

## Board

- **Waveshare ESP32-C3 Zero** (ESP32-C3FH4, 4 MB flash, native USB).
- 15 exposed GPIOs; GPIO12–17 not available (internal flash). Avoid GPIO2 (strapping), GPIO4–7 (JTAG/flash). GPIO9 = BOOT, GPIO10 = onboard WS2812.

## Pin assignment

| Function              | GPIO | Role                    |
|-----------------------|------|-------------------------|
| Drop bus TX (master)  | MOSI | **GPIO3**  (SPI2 MOSI)  |
| Drop bus TX (master)  | CLK  | **GPIO18** (SPI2 SCLK)  |
| Chain bus RX (I2S slave) | CLK  | **GPIO0**  (I2S BCLK in)  |
| Chain bus RX (I2S slave) | MOSI | **GPIO1**  (I2S DIN in)   |
| Chain bus RX (I2S WS)    | WS   | **GPIO8**  (driven low locally if chain has no WS) |

- **SPI2** (HSPI): master for drop-bus TX (MOSI + SCLK only; no MISO/CS).
- **I2S0**: slave for chain-bus RX (BCLK + DIN; WS driven locally on GPIO8 if chain has no WS).
- **USB:** Native USB (CDC) for host ↔ ASCII gateway; no extra pins.

Defines live in [pins.h](pins.h).

## Wiring

- **Drop bus:** Master MOSI (GPIO3) and CLK (GPIO18) go to all slaves (buffered output recommended per [Architecture](../../docs/Architecture.md)).
- **Chain bus:** Last slave in the chain connects its chain output (CLK + MOSI) to master chain RX: CLK → GPIO0, MOSI (data) → GPIO1.

## Chain RX at high speed (I2S + DMA)

The chain RX is **not** implemented as bit-bang (which cannot follow a multi‑MHz clock on ESP32-C3). It uses one of:

- **I2S RX slave (default)** – BCLK = chain CLK, DIN = chain MOSI. The I2S peripheral captures a continuous byte stream via DMA. There is no hardware preamble detection (unlike the RP2040 PIO), so a **sliding-window** parser in software scans the stream for the SpIOpen two-byte preamble (`0xAA 0xAA`), then validates header, DLC, and CRC and pushes complete frames to the RX queue. Frames that span two DMA buffers are handled with a small carryover buffer. WS (word select) is required by I2S; if the chain does not provide it, a local GPIO can drive WS (e.g. tied low) so the interface runs in a single-slot (mono) mode.
- **SPI slave (optional)** – If the chain provides a **frame sync** (e.g. a CS-like line that asserts for the duration of each frame), the master can use the SPI slave driver with DMA and one transaction per frame, avoiding preamble search. Without a sync signal, SPI slave is not suitable for a continuous stream because the driver only completes a transaction when CS de-asserts.

See [main/spiopen_rx_slave.c](main/spiopen_rx_slave.c) and [pins.h](pins.h) for I2S pin and buffer sizing.

## Development environment (ESP-IDF)

If you don’t have ESP-IDF installed yet:

1. **Option A – Windows installer (easiest)**  
   - Download the **ESP-IDF Tools Installer** from [Espressif’s “Get Started”](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/windows-setup.html) (choose “Windows Installer”).
   - Run the installer and pick **ESP-IDF v5.x** and the **ESP32-C3** target. It will set up the toolchain and add an “ESP-IDF 5.x CMD” (and optionally PowerShell) shortcut.

2. **Option B – Manual install**  
   - Clone [esp-idf](https://github.com/espressif/esp-idf) and run `install.ps1` (or `install.bat`) for the required tools, then run `export.ps1` (or `export.bat`) to set `IDF_PATH` and `PATH` in the current shell.  
   - See [Install ESP-IDF on Windows](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/windows-setup.html) for full steps.

3. **Use an ESP-IDF shell**  
   - Always build from a shell where ESP-IDF is active: open **“ESP-IDF 5.x CMD”** (or run `export.ps1` / `export.bat` in your terminal), then `cd` to this project.

## Build

- **ESP-IDF** v5.x (FreeRTOS is built-in).
- From an **ESP-IDF-enabled** shell, in this directory (`firmware/master`):
  - **If you see** *"Directory 'build' doesn't seem to be a CMake build directory"*: remove it first (e.g. a previous non–ESP-IDF CMake run may have created it):
    ```powershell
    Remove-Item -Recurse -Force build
    ```
  - `idf.py set-target esp32c3`
  - `idf.py build`
- Flash and monitor: `idf.py -p <port> flash monitor` (e.g. `COM3` on Windows).

## References

- [Architecture](../../docs/Architecture.md)
- [Development plan](../../docs/DevelopmentPlan.md)
- [lib/spiopen_protocol](../../lib/spiopen_protocol/) and [lib/spiopen_canopen](../../lib/spiopen_canopen/)
