# SpIOpen Algorithm Implementation (Compile-Time Selection)

`SpIOpen_Frame` uses a small algorithm abstraction for:

- CRC16-CCITT
- CRC32 (ISO/MPEG-2)
- SECDED(16,11) encode/decode

The implementation is chosen **at compile time** by linking exactly one implementation `.cpp` file.

## Public API

Include the single header:

- `include/spiopen_frame_algorithms.h`

It declares the types and functions used by the frame writer/reader. Implementations are provided by whichever algorithm implementation source is linked (see below).

## Default Implementation

By default the build links the software implementation:

- `src/spiopen_frame_algorithms.cpp`

This uses the Embedded Template Library (ETL) for CRC and a pure-software SECDED(16,11) implementation.

## Replacing With a Platform-Specific Implementation

To use a hardware-accelerated or other custom implementation:

1. **Implement the same API** in a new `.cpp` file. You must define these symbols in namespace `spiopen::algorithms`:

   - `uint16_t ComputeCrc16Ccitt(const uint8_t* data, size_t length);`
   - `uint32_t ComputeCrc32(const uint8_t* data, size_t length);`
   - `uint16_t Secded16Encode11(uint16_t raw11);`
   - `Secded16DecodeResult Secded16Decode11(uint16_t encoded16);`

   The type `SecdedDecode16Result` is defined in `spiopen_frame_algorithms.h`:

   ```cpp
   struct Secded16DecodeResult {
       uint16_t data11;
       bool corrected;
       bool uncorrectable;
   };
   ```

2. **Point the build at your file** instead of the default implementation. When configuring the library, set the cache variable:

   - `SPIOPEN_FRAME_ALGORITHM_SOURCE` — path to your implementation `.cpp`

   Example (CMake):

   ```cmake
   set(SPIOPEN_FRAME_ALGORITHM_SOURCE
       ${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32/spiopen_frame_algorithm_stm32.cpp
       CACHE FILEPATH "Algorithm implementation" FORCE)
   add_subdirectory(path/to/SpIOpen_Frame)
   ```

   Or when building the library in a custom way, exclude the default algorithm `.cpp` from the build and add your own implementation file so that the linker sees exactly one definition of each algorithm function.

3. **Do not link** both the default implementation and your own; exactly one implementation translation unit should be linked.

## Benefits of This Pattern

- **No runtime cost**: Calls to `ComputeCrc16Ccitt`, etc., resolve directly to your implementation. The compiler can inline if desired (e.g. with LTO).
- **Smaller footprint**: No function-pointer table or dispatcher code.
- **Familiar for embedded**: Same “one header, link the right .cpp” approach often used in C for board-support or HAL code.
