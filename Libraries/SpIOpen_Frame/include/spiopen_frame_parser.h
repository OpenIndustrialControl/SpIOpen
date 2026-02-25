/*
SpIOpen Frame Buffer : Used to link a Frame object to a byte array buffer that contains the entire frame.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_frame.h"
#include "spiopen_frame_format.h"

namespace spiopen::FrameParser {
    /* Constants used to communciate different errors that occur while parsing a SpIOpen frame from a byte array buffer*/
    static constexpr int FRAME_PARSE_ERROR_NO_PREAMBLE = -1; // No preamble found at declared frame start position or search limit reached
    static constexpr int FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_HEADER = -2; // Buffer is too short for the header
    static constexpr int FRAME_PARSE_ERROR_FORMAT_DLC_CORRUPTED = -3; // DLC field was corrupted during parsing (due to multiple bit flips)
    static constexpr int FRAME_PARSE_ERROR_CANFD_NOT_SUPPORTED = -4; // CAN-FD not supported by the frame pool
    static constexpr int FRAME_PARSE_ERROR_CANXL_NOT_SUPPORTED = -5; // CAN-XL not supported by the frame pool
    static constexpr int FRAME_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD = -6; // Buffer is too short for the payload
    static constexpr int FRAME_ERROR_CRC_MISMATCH = -7; // CRC mismatch between calculated and stored CRCs

    //structure that is returned by any function that parses a SpIOpen frame from a byte array buffer
    struct FrameParseResult {
        int error_code; // Error code from the constants above, or 0 if no error occurred
        bool dlc_corrected; // True if the DLC field was corrected during parsing (due to single bit flip)
    };

    //structure that is returned by any function that searches for a SpIOpen frame in a byte array buffer
    struct FrameSearchResult {
        size_t frame_start_offset; // Offset of the first byte of the frame from the start of the buffer (-1 if not found)
        int8_t bit_slip_count; // Number of bit slips detected during the search for the frame (positive for extra bits received, negative for missing bits never received)
    };

    /** 
    * @brief Parse a SpIOpen frame from a byte array buffer. The payload data field of the frame object will point to somewhere within the input buffer.
    * @param buffer Pointer to the byte array buffer to parse the frame from
    * @param buffer_length Length of the byte array buffer
    * @param out_frame Pointer to the Frame object to store the parsed frame
    * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
    * @return FrameParseResult structure containing the error code and DLC correction flag
    */
    static FrameParseResult ParseFrame(const uint8_t *buffer, size_t buffer_length, Frame *out_frame, size_t buffer_offset = 0);

    /** 
    * @brief Parse a SpIOpen frame from a byte array buffer
    * @param buffer Pointer to the byte array buffer to parse the frame from
    * @param buffer_length Length of the byte array buffer
    * @param out_frame Pointer to the Frame object to store the parsed frame
    * @param out_buffer Pointer to the byte array buffer to store the parsed frame
    * @param out_buffer_length Length of the output byte array buffer
    * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
    * @param bit_slip_count Number of bit slips to correct for during the copy operation (positive for extra bits received, negative for missing bits never received)
    * @return FrameParseResult structure containing the error code and DLC correction flag
    */
    static FrameParseResult ParseAndCopyFrame(const uint8_t *buffer, size_t buffer_length, Frame *out_frame, uint8_t *out_buffer, size_t out_buffer_length, size_t buffer_offset = 0, int8_t bit_slip_count = 0);

    /**
    * @brief Search for a SpIOpen frame preamble in a byte array buffer
    * @param buffer Pointer to the byte array buffer to search for the preamble in
    * @param length Length of the byte array buffer
    * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
    * @param bit_slips_allowed True if bit slips are allowed during the search for the preamble
    * @return FrameSearchResult structure containing the offset of the frame and the number of bit slips detected
    */
    static FrameSearchResult FindFrame(const uint8_t *buffer, size_t length, size_t buffer_offset = 0, bool bit_slips_allowed = 0);



}