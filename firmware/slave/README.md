# SpIOpen slave (RP2040 XIAO)

Phase 1: loopback with one native SPI master output and two PIO receivers (drop bus + chain). Target 10 MHz.

**Build:** Requires [Pico SDK](https://github.com/raspberrypi/pico-sdk). Set `PICO_SDK_PATH` or run CMake with `-DPICO_SDK_FETCH_FROM_GIT=ON` to fetch the SDK.

If using the Raspberry Pi "Pico SDK v1.5.1" Windows installer and Ninja is not on PATH, point CMake at the bundled Ninja with `-DCMAKE_MAKE_PROGRAM="C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\ninja\ninja.exe"`.

```bash
cd firmware/slave
mkdir build && cd build
cmake .. -G Ninja -DPICO_SDK_PATH="C:\Program Files\Raspberry Pi\Pico SDK v1.5.1" -DCMAKE_MAKE_PROGRAM="C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\ninja\ninja.exe"
ninja
```
(Or use your PICO_SDK_PATH and pass the same path's `ninja\ninja.exe` for `CMAKE_MAKE_PROGRAM`.)

Flash: copy `spiopen_slave.uf2` to the XIAO RP2040 mass-storage device.

**Phase 1 loopback pins (RP2040 XIAO):** Drop bus (PIO) CLK=GPIO26, MOSI=GPIO27. Chain input (PIO) CLK=GPIO28, MOSI=GPIO29. Chain output SPI CLK=GPIO2, MOSI=GPIO3. Full table and wiring: [docs/DevelopmentPlan.md](../../docs/DevelopmentPlan.md).
