# SpIOpen Frame Library

C++ Library for interacting with SpIOpen Frames. 

## Overview

SpIOpen uses a simplex SPI drop-bus (clock and MOSI, no chip select or MISO) to communicate from a single master to all slaves simultaneously. 
The slaves communicate in a daisy-chain configuration on an identical, but reversed bus, that terminates back at the master. 

The bus is designed around tunneling CAN-CC, CAN-FD, and CAN-XL frames at a target rate of 10MHz to enable CANopen-like behavior of IO slave modules on an industrial backplane. 
The advantage over the CAN physical layer is the lack of Transcievers and removing the open-drain configuration which allows for lower cost higher speed data.
This comes at the expense of some added latency, lack of prioritization-through-arbitration, and decentralization of physical CAN.

The format of the SpIOpen frame is defined in [FrameFormat.md](FrameFormat.md).

## Contents

- spiopen_frame.h : contains the `spiopen::Frame` class which defines structured data types for representing a SpIOpen data frame
- spiopen_frame_format.h contains constants and helper functions relating to the structure of the SpIOpen Frame on the wire
- spiopen_frame_parser.h : Contains the `spiopen::frame_parser` namespace which has helper functions used to parse byte streams (`stl::byte_stream_reader`) into a structured Frame object.
- spiopen_frame_writer.h : Contains the `spiopen::frame_writer` namespace which has helper functions used to write structured Frame objects into a byte stream (`stl::byte_stream_writer`).
- spiopen_frame_algorithms.h : Contains the `spiopen::algorithms` abstraction layer for common algorithms (hamming, CRC) and a default software implementation. See [AlgorithmBackend.md](AlgorithmBackend.md) for more info.
- spiopen_frame_buffer.h contains the `spiopen::FrameBuffer` class which represents a combination of a `Frame` object and a non-owned memory buffer location (`etl::span<uint8_t>`) with helper functions for synchronizing the two of them.

## Configuration

### KConfig

Use KConfig to enable/disable features of the library.
- SPIOPEN_FRAME_CAN_FD_ENABLE enables 64 byte messages (up from a max of 8) and allows tunneling of FD frames.
- SPIOPEN_FRAME_CAN_XL_ENABLE enables 2048 byte messages (up from a max of 64) and allows tunneling of XL frames. Also includes the XL control structure.

### Algorithms

If hardware acceleration is available for algorithms like CRC16 and CRC32, ports can be developed to replace the software calculation with something more optimized. See [AlgorithmBackend.md](AlgorithmBackend.md)

## Usage

This library is generally meant to be used by the SpIOpen_Broker library, where Frames or `FrameBuffer`s are shuffled around between publishers and subscribers. Depending on whether the calling code is a publisher or subscriber, or working on a port or as an app, the usage might be different.

Reading from the wire, no matter the source or method, should *always* result in a synchronization from the internal buffer to the internal `Frame` object. This allows all subscribers to assume that the structured data in the `Frame` object is the source of truth, and all data manipulation will happen at the structured data object level. The internal buffer should not be accessed until it is time to write the data back to the wire, at which point the `Frame` object must be synchronized back to the internal buffer.

### Reading from the Wire (Publisher)

`FrameBuffer` is constructed with a `span<uint8_t>` argument that represents its memory stream. This can be a pointer to a portion of a circular buffer that may change over the lifetime of the `FrameBuffer` object, or potentially a permanently assigned dedicated buffer linked to the `Frame` object (perhaps written via DMA?). The buffer is filled asynchronously, and when the writing process is complete it should call the `ReadInternalBuffer()` function to parse the byte stream into the contained `Frame` object, which represents the frame in a more structured way for future subscribers.

When using external memory to capture streamed data from an input port, there is a potential for bit-misalignment to occur (essentially a right-rotate over the infinite bitstream due to a missed bit). In that case, use the `LoadAndReadInternalBuffer()` function to simultaneously copy and bit-shift the external data into the `FrameBuffer`.

### Writing to the Wire (Subscriber)

A `FrameBuffer` object is passed to a subscriber that wants to write the byte stream to an output port. The subscriber should call the `WriterInternalBuffer()` function before accessing the byte stream to ensure that the latest `Frame` object data has been represented in the buffer (this is necessary even for minor changes like decrementing the TTL because the CRC will be recalculated).

### Writing to the Object (Publisher)

When an application generates SpIOpen Frames (like a CANOpenNode instance) it can either write to the internal `Frame` object directly, or it can copy its own `Frame` object data into the `FrameBuffer` using the `LoadFrameAndWriteInternalBuffer()` function.

### Reading from the Object (Subscriber)

The subscriber can assume that the internal `Frame` object is already loaded and accurate. It should access that data directly and ignore the internal buffer or other functions of the `FrameBuffer` object.

