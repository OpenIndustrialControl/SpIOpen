/*
SpIOpen Router Frame Buffer : Used to link a Frame object to a byte array buffer that contains the entire frame.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once
#include "etl/byte_stream.h"
#include "etl/span.h"
#include "spiopen_frame.h"
#include "spiopen_frame_reader.h"
#include "spiopen_frame_writer.h"

namespace spiopen {

/**
 * @brief Links a Frame object to a byte array buffer that contains the entire frame. The buffer may be updated by
 * external functions (DMA, etc) requiring a re-parsing of the frame.
 * @param buffer Reference to the byte array buffer that is permanently allocated for this FrameBuffer object
 */
class FrameBuffer {
   public:
    FrameBuffer(etl::span<uint8_t> buffer) : buffer_(buffer), frame_() {}
    ~FrameBuffer() = default;

    // Functions that synchronize the internal frame object with the internal buffer
    /**
     * @brief Writes the internal frame object to the internal buffer.
     *
     * If the payload points to an external buffer, the data will be copied to the internal buffer and the pointer will
     * be adjusted to reference the internal buffer. This is useful for when an external process modifies the frame
     * object.
     *
     * @return On success, void; on failure, the error code
     */
    etl::expected<void, frame_writer::FrameWriteError> UpdateInternalBuffer() {
        etl::byte_stream_writer writer(buffer_, etl::endian::big);
        return frame_writer::WriteFrame(writer, frame_);
    }

    /**
     * @brief Updates the internal frame object based on the internal buffer
     *
     * The payload data field of the frame object will be updated to point to somewhere within the internal buffer.
     * This is useful for when an external process fills the buffer with a new frame.
     *
     * @note The function may fail due to a parsing error. If it does, the internal frame object will be in an undefined
     * state.
     * @return On success, FrameReadResult with dlc_corrected flag; on failure, the parse error
     */
    etl::expected<frame_reader::FrameReadResult, frame_reader::FrameParseError> UpdateInternalFrame() {
        etl::byte_stream_reader reader(buffer_.data(), buffer_.size(), etl::endian::big);
        return frame_reader::ReadFrame(reader, frame_);
    }

    // Functions that set the internal fields based on external data
    /**
     * @brief Reads the frame from the provided stream and copies it into the internal buffer, but does not update the
     * internal frame object. May fail if the buffer is too short to contain the frame or cannot be parsed..
     *
     * @note The function may fail due to a parsing error. If it does, the internal frame object will be in an undefined
     * state.
     * @param input_stream Byte stream reader positioned at the start of the frame (preamble). Uses big-endian.
     * @param bit_slip_count Number of bit slips to correct for during the copy operation (positive for extra bits
     * received, padding the most significant bits of the first byte)
     * @return On success, FrameReadResult with dlc_corrected flag; on failure, the parse error
     */
    etl::expected<frame_reader::FrameReadResult, frame_reader::FrameParseError> CopyToInternalBuffer(
        etl::byte_stream_reader &input_stream, uint8_t bit_slip_count = 0) {
        return frame_reader::ReadAndCopyFrame(input_stream, buffer_, frame_, bit_slip_count);
    }

    /**
     * @brief Copies the provided frame object to the internal frame object, but does not write the internal frame
     * object to the internal buffer. may fail if the buffer is too short to contain the frame.
     *
     * @param frame Reference to the Frame object to load and write to the internal buffer
     * @return On success, void; on failure, the error code
     */
    etl::expected<void, frame_writer::FrameWriteError> CopyToInternalFrame(Frame const &frame) {
        size_t frame_length = 0;
        if (!frame.TryGetFrameLength(frame_length)) {
            return etl::unexpected(frame_writer::FrameWriteError::InvalidFrameLength);
        }
        if (frame_length > buffer_.size()) {
            return etl::unexpected(frame_writer::FrameWriteError::BufferTooShort);
        }
        frame_ = frame;
        return {};
    }

    // Getters for the internal fields
    Frame &GetFrame() { return frame_; }
    etl::span<uint8_t> GetBuffer() { return buffer_; }
    void SetBuffer(etl::span<uint8_t> buffer) { buffer_ = buffer; }

   private:
    Frame frame_;
    etl::span<uint8_t> buffer_;
};

}  // namespace spiopen