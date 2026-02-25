/*
SpIOpen Frame Writer : Used to write a Frame object to a byte array buffer.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_frame.h"
#include "spiopen_frame_format.h"

namespace spiopen::FrameWriter {
    /* Constants used to communciate different errors that occur while writing a SpIOpen frame to a byte array buffer*/
    static constexpr int FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH = -1; // The data length is invalid for the frame type
    static constexpr int FRAME_WRITE_ERROR_INVALID_PAYLOAD_POINTER = -2; // The payload pointer is invalid or NULL
    static constexpr int FRAME_WRITE_ERROR_BUFFER_TOO_SHORT = -3; // The buffer is too short to write the frame

    //structure that is returned by any function that writes a SpIOpen frame to a byte array buffer
    struct FrameWriteResult {
        int error_code; // Error code from the constants above, or 0 if no error occurred
        size_t payload_padding_added; // Total length of the payload padding added to the frame in bytes to meet the Data Length Code (DLC) requirements
        size_t frame_padding_added; // Total length of the frame padding added to the frame in bytes to meet the Word Alignment requirements
        size_t total_length; // Total length of the frame in bytes, from the start of the preamble to the end of the CRC
    } ;

    /**
    * @brief Writes a SpIOpen frame to a byte array buffer
    * @param frame Pointer to the Frame object to write
    * @param buffer Pointer to the byte array buffer to write the frame to
    * @param buffer_length Length of the byte array buffer
    * @return FrameWriteResult structure containing the error code, payload padding added, and frame padding added
    */
    static FrameWriteResult WriteFrame(const Frame *frame, uint8_t *buffer, size_t buffer_length);
}