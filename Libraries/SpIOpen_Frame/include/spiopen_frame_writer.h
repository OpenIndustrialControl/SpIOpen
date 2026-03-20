/*
SpIOpen Frame Writer : Used to write a Frame object to a byte array buffer.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>

#include "etl/byte_stream.h"
#include "etl/expected.h"
#include "spiopen_frame.h"
#include "spiopen_frame_format.h"

namespace spiopen::frame_writer {

/** Error codes for frame write operations. */
enum class FrameWriteError : uint8_t {
    InvalidPayloadLength = 1,  // The data length is invalid for the frame type
    InvalidFrameLength,        // The frame length is invalid for the frame type
    InvalidPayloadPointer,     // The payload pointer is invalid or NULL
    BufferTooShort,            // The buffer is too short to write the frame
    InvalidBufferPointer,      // The buffer pointer is invalid or NULL
    InvalidFramePointer,       // The frame pointer is invalid or NULL
    CanXlNotSupported,         // CAN-XL not supported by this build
};

/**
 * @brief Writes a SpIOpen frame to a byte stream writer
 * @param stream Reference to the byte stream writer to write the frame to
 * @param frame Reference to the Frame object to write
 * @return On success, void; on failure, the error code
 */
etl::expected<void, FrameWriteError> WriteFrame(etl::byte_stream_writer& stream, const Frame& frame);

// Helper functions for writing a SpIOpen frame to a byte array buffer. Not to be accessed directly.
namespace impl {
etl::expected<void, FrameWriteError> ValidateFrame(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WritePreamble(etl::byte_stream_writer& stream);
etl::expected<void, FrameWriteError> WriteFormatHeader(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WriteCanIdentifier(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WriteXlDataAndControl(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WriteTimeToLive(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WritePayload(etl::byte_stream_writer& stream, const Frame& frame);
etl::expected<void, FrameWriteError> WriteCrc(etl::byte_stream_writer& stream, const Frame& frame,
                                              const etl::span<const uint8_t>& crc_region);
etl::expected<void, FrameWriteError> WriteFramePadding(etl::byte_stream_writer& stream, const Frame& frame);
}  // namespace impl
}  // namespace spiopen::frame_writer
