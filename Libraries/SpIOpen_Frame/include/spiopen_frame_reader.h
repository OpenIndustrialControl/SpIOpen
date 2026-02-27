/*
SpIOpen Frame Buffer : Used to link a Frame object to a byte array buffer that contains the entire frame.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_frame.h"
#include "spiopen_frame_format.h"

namespace spiopen::frame_reader {
/* Constants used to communciate different errors that occur while parsing a SpIOpen frame from a byte array buffer*/
static constexpr int FRAME_PARSE_ERROR_NO_PREAMBLE =
    -1;  // No preamble found at declared frame start position or search limit reached
static constexpr int FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH =
    -2;  // Buffer is too short to determine the length of the frame based on a partial header read
static constexpr int FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_HEADER = -3;  // Buffer is too short for the header
static constexpr int FRAME_PARSE_ERROR_FORMAT_DLC_CORRUPTED =
    -4;  // DLC field was corrupted during parsing (due to multiple bit flips)
static constexpr int FRAME_PARSE_ERROR_CANFD_NOT_SUPPORTED = -5;           // CAN-FD not supported by the frame pool
static constexpr int FRAME_PARSE_ERROR_CANXL_NOT_SUPPORTED = -6;           // CAN-XL not supported by the frame pool
static constexpr int FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD = -7;  // Buffer is too short for the payload
static constexpr int FRAME_PARSE_ERROR_CRC_MISMATCH = -8;            // CRC mismatch between calculated and stored CRCs
static constexpr int FRAME_PARSE_ERROR_INVALID_BUFFER_POINTER = -9;  // Buffer pointer is invalid or NULL
static constexpr int FRAME_PARSE_ERROR_INVALID_FRAME_POINTER = -10;  // Frame pointer is invalid or NULL
static constexpr int FRAME_PARSE_ERROR_DLC_INVALID =
    -11;  // DLC field was successfully decoded but indicates an invalid (too large) value
static constexpr int FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PREAMBLE = -12;  // Buffer is too short for the preamble

// structure that is returned by any function that parses a SpIOpen frame from a byte array buffer
struct FrameReadResult {
    int error_code;      // Error code from the constants above, or 0 if no error occurred
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
                                // the buffer (-1 if not found)
    int8_t bit_slip_count;      // Number of bit slips detected during the search for the frame (positive for extra bits
                                // received, to a maximum of 7. 0 indicates no bit slips)
};

/**
 * @brief Read a SpIOpen frame from a byte array buffer. The payload data field of the frame object will point to
 * somewhere within the input buffer.
 * @param buffer Pointer to the byte array buffer to parse the frame from
 * @param buffer_length Length of the byte array buffer
 * @param out_frame Pointer to the Frame object to store the read frame
 * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
 * @return FrameReadResult structure containing the error code and DLC correction flag
 */
static FrameReadResult ReadFrame(const uint8_t *buffer, size_t buffer_length, Frame *out_frame,
                                 size_t buffer_offset = 0);

/**
 * @brief Read a SpIOpen frame from a byte array buffer
 * @param source_buffer Pointer to the byte array buffer to read the frame from
 * @param source_buffer_length Length of the source byte array buffer
 * @param out_frame Pointer to the Frame object to store the read frame
 * @param out_buffer Pointer to the byte array buffer to store the read frame
 * @param out_buffer_length Length of the output byte array buffer
 * @param source_buffer_offset Offset of the first byte of the frame from the start of the source buffer
 * @param bit_slip_count Number of bit slips to correct for during the copy operation (positive for extra bits received,
 * to a maximum of 7. 0 indicates no bit slips)
 * @return FrameReadResult structure containing the error code and DLC correction flag
 */
static FrameReadResult ReadAndCopyFrame(const uint8_t *source_buffer, size_t source_buffer_length, Frame *out_frame,
                                        uint8_t *out_buffer, size_t out_buffer_length, size_t source_buffer_offset = 0,
                                        uint8_t bit_slip_count = 0);

/**
 * @brief Search for a SpIOpen frame preamble in a byte array buffer
 * @param buffer Pointer to the byte array buffer to search for the preamble in
 * @param length Length of the byte array buffer
 * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
 * @param bit_slips_allowed True if bit slips are allowed during the search for the preamble
 * @return FrameSearchResult structure containing the offset of the preamble and the number of bit slips detected
 */
static FrameSearchResult FindFramePreamble(const uint8_t *buffer, size_t length, size_t buffer_offset = 0,
                                           bool bit_slips_allowed = 0);

// /**
//  * @brief Validate a SpIOpen frame in a byte array buffer
//  * @param buffer Pointer to the byte array buffer to validate the frame in
//  * @param length Length of the byte array buffer
//  * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
//  * @param bit_slip_count Number of bit slips to correct for during the validation operation (positive for extra bits
//  * received, to a maximum of 7. 0 indicates no bit slips)
//  * @return FrameValidity structure containing the validity of the frame
//  */
// static FrameValidity ValidateFrame(const uint8_t *buffer, size_t length, size_t buffer_offset = 0,
//                                    uint8_t bit_slip_count = 0);

}  // namespace spiopen::frame_reader