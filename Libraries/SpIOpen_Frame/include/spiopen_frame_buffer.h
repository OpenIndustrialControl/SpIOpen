/*
SpIOpen Frame Buffer : Used to link a Frame object to a byte array buffer that contains the entire frame.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_frame.h"
#include "spiopen_frame_reader.h"
#include "spiopen_frame_writer.h"

namespace spiopen {

/**
 * @brief Links a Frame object to a byte array buffer that contains the entire frame. The buffer may be updated by
 * external functions (DMA, etc) requiring a re-parsing of the frame.
 * @param buffer Pointer to the byte array buffer that is permanently allocated for this FrameBuffer object
 * @param buffer_length Length of the byte array buffer
 */
class FrameBuffer {
   public:
    FrameBuffer(uint8_t *buffer, size_t buffer_length) : buffer_(buffer), buffer_length_(buffer_length) {}
    ~FrameBuffer();

    // Functions that synchronize the internal frame object with the internal buffer
    /**
     * @brief Writes the internal frame object to the internal buffer.
     *
     * If the payload points to an external buffer, the data will be copied to the internal buffer and the pointer will
     * be adjusted to reference the internal buffer. This is useful for when an external process modifies the frame
     * object.
     *
     * @return FrameWriteResult structure containing the error code, payload padding added, and frame padding added
     */
    frame_writer::FrameWriteResult WriteInternalBuffer() {
        return frame_writer::WriteFrame(&frame_, buffer_, buffer_length_);
    }

    /**
     * @brief Updates the internal frame object based on the internal buffer
     *
     * The payload data field of the frame object will be updated to point to somewhere within the input buffer.
     * This is useful for when an external process fills the buffer with a new frame.
     *
     * @note The function may fail due to a parsing error. If it does, the internal frame object will be in an undefined
     * state.
     * @return FrameReadResult structure containing the error code and DLC correction flag
     */
    frame_reader::FrameReadResult ReadInternalBuffer() {
        return frame_reader::ReadFrame(buffer_, buffer_length_, &frame_);
    }

    // Functions that set the internal fields based on external data
    /**
     * @brief Reads the frame from the provided buffer
     *
     * Reads the frame from the provided buffer, adjusting for bit slip if necessary
     * The payload data field of the frame object will be updated to point to somewhere within the input buffer.
     * This is useful for when an external process fills the buffer with a new frame.
     *
     * @note The function may fail due to a parsing error. If it does, the internal frame object will be in an undefined
     * state.
     * @param buffer Pointer to the byte array buffer to read the frame from
     * @param length Length of the byte array buffer
     * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
     * @param bit_slip_count Number of bit slips to correct for during the copy operation (positive for extra bits
     * received, negative for missing bits never received)
     * @return FrameReadResult structure containing the error code and DLC correction flag
     */
    frame_reader::FrameReadResult LoadAndReadInternalBuffer(const uint8_t *buffer, size_t length,
                                                            size_t buffer_offset = 0, int8_t bit_slip_count = 0) {
        return frame_reader::ReadAndCopyFrame(buffer, length, &frame_, buffer_, buffer_length_, buffer_offset,
                                              bit_slip_count);
    }

    /**
     * @brief Copies the provided frame object to the internal frame object, then writes the internal frame object to
     * the internal buffer
     *
     * Copies the data from the provided frame object to the internal frame object, then writes the internal frame
     * object to the internal buffer. The payload data field of the frame object will be updated to point to somewhere
     * within the internal buffer. This is useful for when an external process finds and parses the frame from a
     * different buffer, or creates a new frame object.
     *
     * @param frame Reference to the Frame object to load and write to the internal buffer
     * @return FrameWriteResult structure containing the error code, payload padding added, and frame padding added
     */
    frame_writer::FrameWriteResult LoadFrameAndWriteInternalBuffer(Frame const &frame) {
        frame_ = frame;
        return WriteInternalBuffer();
    }

    // Getters for the internal fields
    Frame &GetFrame() { return frame_; }
    uint8_t *GetBuffer() { return buffer_; }
    size_t GetBufferLength() const { return buffer_length_; }

   private:
    Frame frame_;
    uint8_t *buffer_;
    size_t buffer_length_;
};

}  // namespace spiopen