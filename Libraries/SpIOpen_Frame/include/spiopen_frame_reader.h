/*
SpIOpen Frame Reader : Read and parse SpIOpen frames from byte streams.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>

#include "etl/byte_stream.h"
#include "etl/expected.h"
#include "etl/span.h"
#include "spiopen_frame.h"
#include "spiopen_frame_format.h"

namespace spiopen::frame_reader {

/** Error codes for frame parse/read operations. */
enum class FrameParseError : uint8_t {
    NoPreamble = 1,                   // No preamble found at declared frame start position or search limit reached
    BufferTooShortForPreamble,        // Buffer is too short for the preamble
    BufferTooShortToDetermineLength,  // Buffer is too short to determine frame length from partial header
    BufferTooShortForHeader,          // Buffer is too short for the header
    FormatDlcCorrupted,               // DLC field was corrupted during parsing (due to multiple bit flips)
    CanXlNotSupported,                // CAN-XL not supported by this build
    BufferTooShortForPayload,         // Buffer is too short for the payload
    CrcMismatch,                      // CRC mismatch between calculated and stored CRCs
    DlcInvalid,                       // DLC field was successfully decoded but indicates an invalid (too large) value
    InvalidBitSlipCount,              // Bit slip count is invalid (must be between 0 and 7)
    InvalidPayloadLength,             // Payload length is invalid for the frame type
    InvalidFrameLength,               // Frame length could not be determined from the parsed header
};

/** Result of a frame read/parse operation. */
struct FrameReadResult {
    bool dlc_corrected;  // True if the DLC field was corrected during parsing (due to single bit flip)
};

// enum class FrameValidity {
//     NO_PREAMBLE,          // Unable to find preamble at the cursor position
//     NO_HEADER_IN_BUFFER,  // Unable to read enough of the header to determine the data length
//     DLC_RECOVERED,        // DLC field was corrected during parsing (due to single bit flip)
//     DLC_CORRUPTED,        // DLC field was corrupted during parsing (due to multiple bit flips)
//     NO_CRC_IN_BUFFER,     // Unable to read enough of the CRC to validate the frame
//     CRC_MISMATCH,         // CRC mismatch between calculated and stored CRCs
//     VALID                 // Frame is valid and ready to be used
// };

// structure that is returned by any function that searches for a SpIOpen frame in a byte array buffer
struct FrameSearchResult {
    size_t frame_start_offset;  // Offset of the first byte of the frame (full or partial preamble) from the start of
                                // the buffer
    int8_t bit_slip_count;      // Number of bit slips detected during the search for the frame (positive for extra bits
                                // received, to a maximum of 7. 0 indicates no bit slips)
    bool valid_preamble_found;  // True if a valid preamble was found
};

/**
 * @brief Read a SpIOpen frame from a byte stream. On success, the frame's payload span points into the stream's
 * buffer.
 * @param stream Byte stream reader positioned at the start of the frame (preamble). Uses big-endian.
 * @param out_frame Pointer to the Frame object to store the read frame
 * @return On success, FrameReadResult with dlc_corrected flag; on failure, the parse error
 */
etl::expected<FrameReadResult, FrameParseError> ReadFrame(etl::byte_stream_reader& stream, Frame& out_frame);

/**
 * @brief Read a SpIOpen frame from an input byte stream, copy it with optional bit-slip correction into a
 * destination buffer, and parse the result into the frame.
 * @param input_stream Byte stream reader positioned at the start of the frame (preamble). Uses big-endian.
 * @param destination_buffer Span of the buffer to receive the copied frame bytes
 * @param out_frame Pointer to the Frame object to store the read frame (payload will point into destination_buffer)
 * @param bit_slip_count Number of bit slips to correct for (0 to 7; positive for extra bits received)
 * @return On success, FrameReadResult with dlc_corrected flag; on failure, the parse error
 */
etl::expected<FrameReadResult, FrameParseError> ReadAndCopyFrame(etl::byte_stream_reader& input_stream,
                                                                 etl::span<uint8_t> destination_buffer,
                                                                 Frame& out_frame, uint8_t bit_slip_count);

/**
 * @brief Search for a SpIOpen frame preamble in a buffer.
 * @param buffer Span of the byte array to search for the preamble in
 * @param offset Byte offset into the buffer at which to start searching (default 0)
 * @param bit_slips_allowed If true, allow bit-slip correction and search for complement preamble (default true)
 * @return FrameSearchResult with frame_start_offset, bit_slip_count, and valid_preamble_found
 */
FrameSearchResult FindNextFramePreamble(const etl::span<uint8_t>& buffer, size_t offset = 0,
                                        bool bit_slips_allowed = true);

/** Helper functions; exposed for testing only. */
namespace impl {
etl::expected<void, FrameParseError> ParseFormatHeader(const uint8_t high, const uint8_t low, Frame& frame,
                                                       bool& dlc_corrected, size_t& payload_len_out);
etl::expected<void, FrameParseError> ValidatePreamble(etl::byte_stream_reader& stream);
etl::expected<void, FrameParseError> ReadFormatHeader(etl::byte_stream_reader& stream, Frame& out_frame,
                                                      bool& dlc_corrected, size_t& payload_len_out);
etl::expected<void, FrameParseError> ReadXlPayloadLength(etl::byte_stream_reader& stream, Frame& out_frame,
                                                         bool& dlc_corrected, size_t& payload_len_out);
etl::expected<void, FrameParseError> ReadXlControl(etl::byte_stream_reader& stream, Frame& out_frame);
etl::expected<void, FrameParseError> ReadCanID(etl::byte_stream_reader& stream, Frame& out_frame);
etl::expected<void, FrameParseError> ReadTTL(etl::byte_stream_reader& stream, Frame& out_frame);
etl::expected<void, FrameParseError> ValidateCRC(etl::byte_stream_reader& stream, const Frame& frame,
                                                 const etl::span<const uint8_t>& crc_region);
bool CopyFromBitSlippedBuffer(etl::byte_stream_reader& source, etl::byte_stream_writer& dest, size_t bytes_to_copy,
                              uint8_t bit_slip_count);
etl::expected<size_t, FrameParseError> FindNextPreambleByte(const etl::span<uint8_t>& buffer, size_t offset = 0,
                                                            bool bit_slips_allowed = true);
etl::expected<uint8_t, FrameParseError> CountBitOffsetIntoPreviousByte(const etl::span<uint8_t>& buffer,
                                                                       size_t preamble_index = 0);
}  // namespace impl

}  // namespace spiopen::frame_reader