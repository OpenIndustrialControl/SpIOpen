/*
SpIOpen Frame Reader : Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame_algorithms.h"

namespace spiopen::frame_reader {

using namespace spiopen::format;

static inline bool CanRead(const size_t offset, const size_t needed, const size_t length) {
    return (offset <= length) && (needed <= (length - offset));
}

static inline size_t DecodePayloadLength(const uint8_t dlc_nibble, const Frame::Flags& flags) {
    if (flags.XLF) {
        return 0U;  // For XL, payload length comes from XL control.
    }
    if (flags.FDF) {
        return CAN_FD_PAYLOAD_BY_DLC[dlc_nibble & HEADER_DLC_MASK];
    }
    const size_t cc_length = static_cast<size_t>(dlc_nibble & HEADER_DLC_MASK);
    return (cc_length > MAX_CC_PAYLOAD_SIZE) ? MAX_CC_PAYLOAD_SIZE : cc_length;
}

static int ParseFormatHeader(const uint8_t high, const uint8_t low, Frame& frame, bool& dlc_corrected) {
    const uint16_t encoded_header = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
    const algorithms::Secded16DecodeResult decoded = algorithms::Secded16Decode11(encoded_header);
    if (decoded.uncorrectable) {
        return FRAME_PARSE_ERROR_FORMAT_DLC_CORRUPTED;
    }
    dlc_corrected = dlc_corrected || decoded.corrected;

    const uint16_t raw_header11 = decoded.data11;
    const uint8_t dlc_nibble = static_cast<uint8_t>(raw_header11 & HEADER_DLC_MASK);
    frame.can_flags.IDE = (raw_header11 & static_cast<uint16_t>(HEADER_IDE_MASK)) != 0U;
    frame.can_flags.FDF = (raw_header11 & static_cast<uint16_t>(HEADER_FDF_MASK)) != 0U;
    frame.can_flags.XLF = (raw_header11 & static_cast<uint16_t>(HEADER_XLF_MASK)) != 0U;
    frame.can_flags.TTL = (raw_header11 & static_cast<uint16_t>(HEADER_TTL_MASK)) != 0U;
    frame.can_flags.WA = ((raw_header11 >> 8U) & HEADER_WA_MASK) != 0U;

    // For non-XL frames, payload length is derived from DLC here.
    // For XL, this value will be zero, and will be replaced by the XL payload length field later.
    frame.payload_length = DecodePayloadLength(dlc_nibble, frame.can_flags);
    return 0;
}

static int ValidatePreamble(const uint8_t* buffer, const size_t buffer_length, const size_t cursor) {
    if (!CanRead(cursor, PREAMBLE_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH;
    }
    if ((buffer[cursor] != PREAMBLE_BYTE) || (buffer[cursor + 1U] != PREAMBLE_BYTE)) {
        return FRAME_PARSE_ERROR_NO_PREAMBLE;
    }
    return 0;
}

static int ReadFormatHeader(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame,
                            bool& dlc_corrected) {
    if (!CanRead(cursor, FORMAT_HEADER_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH;
    }

    const int parse_result = ParseFormatHeader(buffer[cursor], buffer[cursor + 1U], *out_frame, dlc_corrected);
    if (parse_result != 0) {
        return parse_result;
    }

#ifndef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    if (out_frame->can_flags.FDF) {
        return FRAME_PARSE_ERROR_CANFD_NOT_SUPPORTED;
    }
#endif
#ifndef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (out_frame->can_flags.XLF) {
        return FRAME_PARSE_ERROR_CANXL_NOT_SUPPORTED;
    }
#endif
    return 0;
}

static int ReadXlPayloadLength(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame,
                               bool& dlc_corrected) {
    // Assume that this is an XL frame if the function is being called.
    // The caller assumes that this function will read two bytes.

    // In XL, the first two bytes immediately after the format header are payload length.
    if (!CanRead(cursor, XL_DATA_LENGTH_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH;
    }

    const uint16_t encoded_xl_dlc = static_cast<uint16_t>((static_cast<uint16_t>(buffer[cursor]) << 8U) |
                                                          static_cast<uint16_t>(buffer[cursor + 1U]));
    const algorithms::Secded16DecodeResult decoded = algorithms::Secded16Decode11(encoded_xl_dlc);
    if (decoded.uncorrectable) {
        return FRAME_PARSE_ERROR_FORMAT_DLC_CORRUPTED;
    }
    dlc_corrected = dlc_corrected || decoded.corrected;

    out_frame->payload_length = static_cast<size_t>(decoded.data11);
    if (out_frame->payload_length > MAX_XL_PAYLOAD_SIZE) {
        return FRAME_PARSE_ERROR_DLC_INVALID;
    }
    return 0;
}

static int ReadXlControl(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame) {
    // Assume that this is an XL frame if the function is being called.
    // The caller assumes that this function will read six bytes.
    if (!CanRead(cursor, XL_CONTROL_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_HEADER;
    }
    out_frame->xl_control.payload_type = buffer[cursor];
    out_frame->xl_control.virtual_can_network_id = buffer[cursor + 1U];
    out_frame->xl_control.addressing_field =
        (static_cast<uint32_t>(buffer[cursor + 2U]) << 24U) | (static_cast<uint32_t>(buffer[cursor + 3U]) << 16U) |
        (static_cast<uint32_t>(buffer[cursor + 4U]) << 8U) | static_cast<uint32_t>(buffer[cursor + 5U]);
    return 0;
}

static int ReadCanID(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame) {
    size_t byte_length =
        out_frame->can_flags.IDE ? (CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE) : CAN_IDENTIFIER_SIZE;
    if (!CanRead(cursor, byte_length, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_HEADER;
    }
    // CAN identifier chunk 1: first two bytes always present and carry RTR/BRS/ESI.
    const uint8_t cid_b0 = buffer[cursor];
    const uint8_t cid_b1 = buffer[cursor + 1U];
    out_frame->can_flags.RTR = (cid_b0 & CID_RTR_MASK) != 0U;
    out_frame->can_flags.BRS = (cid_b0 & CID_BRS_MASK) != 0U;
    out_frame->can_flags.ESI = (cid_b0 & CID_ESI_MASK) != 0U;

    // CAN identifier chunk 2: extension only when IDE is set.
    if (out_frame->can_flags.IDE) {
        const uint8_t cid_b2 = buffer[cursor + 2U];
        const uint8_t cid_b3 = buffer[cursor + 3U];
        out_frame->can_identifier =
            (static_cast<uint32_t>(cid_b0 & static_cast<uint8_t>(~(CID_RTR_MASK | CID_BRS_MASK | CID_ESI_MASK)))
             << 24U) |
            (static_cast<uint32_t>(cid_b1) << 16U) | (static_cast<uint32_t>(cid_b2) << 8U) |
            static_cast<uint32_t>(cid_b3);
    } else {
        out_frame->can_identifier =
            (static_cast<uint32_t>(cid_b0 & static_cast<uint8_t>(~(CID_RTR_MASK | CID_BRS_MASK | CID_ESI_MASK)))
             << 8U) |
            static_cast<uint32_t>(cid_b1);
    }
    return 0;
}

static int ReadTTL(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame) {
    // Assume that this is a frame with a TTL field if the function is being called.
    // The caller assumes that this function will read one byte.
    if (!CanRead(cursor, TIME_TO_LIVE_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_HEADER;
    }
    out_frame->time_to_live = buffer[cursor];
    return 0;
}

static int ValidateCRC(const uint8_t* buffer, const size_t buffer_length, const size_t frame_start_offset,
                       const Frame* frame) {
    const size_t frame_length = frame->GetFrameLength();
    if (!CanRead(frame_start_offset, frame_length, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD;
    }

    const size_t crc_region_start = frame_start_offset + PREAMBLE_SIZE;
    const size_t crc_size = (frame->payload_length > MAX_CC_PAYLOAD_SIZE) ? LONG_CRC_SIZE : SHORT_CRC_SIZE;
    const size_t crc_offset = frame_start_offset + frame_length - crc_size;
    size_t cursor = crc_offset;
    const size_t crc_region_len = crc_offset - crc_region_start;
    if (crc_size == SHORT_CRC_SIZE) {
        const uint16_t received_crc = static_cast<uint16_t>((static_cast<uint16_t>(buffer[cursor]) << 8U) |
                                                            static_cast<uint16_t>(buffer[cursor + 1U]));
        const uint16_t computed_crc = algorithms::ComputeCrc16Ccitt(buffer + crc_region_start, crc_region_len);
        if (computed_crc != received_crc) {
            return FRAME_PARSE_ERROR_CRC_MISMATCH;
        }
    } else {
        const uint32_t received_crc =
            (static_cast<uint32_t>(buffer[cursor]) << 24U) | (static_cast<uint32_t>(buffer[cursor + 1U]) << 16U) |
            (static_cast<uint32_t>(buffer[cursor + 2U]) << 8U) | static_cast<uint32_t>(buffer[cursor + 3U]);
        const uint32_t computed_crc = algorithms::ComputeCrc32(buffer + crc_region_start, crc_region_len);
        if (computed_crc != received_crc) {
            return FRAME_PARSE_ERROR_CRC_MISMATCH;
        }
    }

    return 0;
}

static int CopyFromBitSlippedBuffer(const uint8_t* source_buffer, const size_t source_buffer_length,
                                    const size_t source_offset, uint8_t* destination_buffer,
                                    const size_t destination_buffer_length, const size_t destination_offset,
                                    const size_t bytes_to_copy, const uint8_t bit_slip_count) {
    if (!CanRead(destination_offset, bytes_to_copy, destination_buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD;
    }
    if (bit_slip_count > 7U) {
        return FRAME_PARSE_ERROR_NO_PREAMBLE;
    }
    if (bit_slip_count == 0U) {
        if (!CanRead(source_offset, bytes_to_copy, source_buffer_length)) {
            return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD;
        }
        std::memcpy(destination_buffer + destination_offset, source_buffer + source_offset, bytes_to_copy);
        return 0;
    }

    if (!CanRead(source_offset, bytes_to_copy + 1U, source_buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PAYLOAD;
    }
    for (size_t i = 0U; i < bytes_to_copy; ++i) {
        const uint8_t high = source_buffer[source_offset + i];
        const uint8_t low = source_buffer[source_offset + i + 1U];
        destination_buffer[destination_offset + i] =
            static_cast<uint8_t>((static_cast<uint16_t>(high) << bit_slip_count) | (low >> (8U - bit_slip_count)));
    }
    return 0;
}

FrameReadResult ReadFrame(const uint8_t* buffer, size_t buffer_length, Frame* out_frame, size_t buffer_offset) {
    FrameReadResult result{};
    result.error_code = 0;
    result.dlc_corrected = false;

    if (buffer == nullptr) {
        result.error_code = FRAME_PARSE_ERROR_INVALID_BUFFER_POINTER;
        return result;
    }
    if (out_frame == nullptr) {
        result.error_code = FRAME_PARSE_ERROR_INVALID_FRAME_POINTER;
        return result;
    }

    out_frame->Reset();
    size_t cursor = buffer_offset;

    result.error_code = ValidatePreamble(buffer, buffer_length, cursor);
    if (result.error_code != 0) {
        return result;
    }
    cursor += PREAMBLE_SIZE;

    result.error_code = ReadFormatHeader(buffer, buffer_length, cursor, out_frame, result.dlc_corrected);
    if (result.error_code != 0) {
        return result;
    }
    cursor += FORMAT_HEADER_SIZE;

    if (out_frame->can_flags.XLF) {
        result.error_code = ReadXlPayloadLength(buffer, buffer_length, cursor, out_frame, result.dlc_corrected);
        if (result.error_code != 0) {
            return result;
        }
        cursor += XL_DATA_LENGTH_SIZE;

        result.error_code = ReadXlControl(buffer, buffer_length, cursor, out_frame);
        if (result.error_code != 0) {
            return result;
        }
        cursor += XL_CONTROL_SIZE;
    }

    result.error_code = ReadCanID(buffer, buffer_length, cursor, out_frame);
    if (result.error_code != 0) {
        return result;
    }
    cursor += (out_frame->can_flags.IDE) ? (CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE) : CAN_IDENTIFIER_SIZE;

    if (out_frame->can_flags.TTL) {
        result.error_code = ReadTTL(buffer, buffer_length, cursor, out_frame);
        if (result.error_code != 0) {
            return result;
        }
        cursor += TIME_TO_LIVE_SIZE;
    }

    out_frame->payload_data = const_cast<uint8_t*>(buffer + cursor);

    result.error_code = ValidateCRC(buffer, buffer_length, buffer_offset, out_frame);
    if (result.error_code != 0) {
        return result;
    }

    return result;
}

FrameReadResult ReadAndCopyFrame(const uint8_t* source_buffer, size_t source_buffer_length, Frame* out_frame,
                                 uint8_t* out_buffer, size_t out_buffer_length, size_t source_buffer_offset,
                                 uint8_t bit_slip_count) {
    FrameReadResult result{};
    result.error_code = 0;
    result.dlc_corrected = false;

    if (source_buffer == nullptr) {
        result.error_code = FRAME_PARSE_ERROR_INVALID_BUFFER_POINTER;
        return result;
    }
    if (out_buffer == nullptr) {
        result.error_code = FRAME_PARSE_ERROR_INVALID_BUFFER_POINTER;
        return result;
    }
    if (out_frame == nullptr) {
        result.error_code = FRAME_PARSE_ERROR_INVALID_FRAME_POINTER;
        return result;
    }

    out_frame->Reset();
    size_t cursor = 0U;

    result.error_code = CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor,
                                                 out_buffer, out_buffer_length, cursor, PREAMBLE_SIZE, bit_slip_count);
    if (result.error_code != 0) {
        return result;
    }

    result.error_code = ValidatePreamble(out_buffer, out_buffer_length, cursor);
    if (result.error_code != 0) {
        return result;
    }
    cursor += PREAMBLE_SIZE;

    result.error_code =
        CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor, out_buffer,
                                 out_buffer_length, cursor, FORMAT_HEADER_SIZE, bit_slip_count);
    if (result.error_code != 0) {
        return result;
    }

    result.error_code = ReadFormatHeader(out_buffer, out_buffer_length, cursor, out_frame, result.dlc_corrected);
    if (result.error_code != 0) {
        return result;
    }
    cursor += FORMAT_HEADER_SIZE;

    if (out_frame->can_flags.XLF) {
        result.error_code =
            CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor, out_buffer,
                                     out_buffer_length, cursor, XL_DATA_LENGTH_SIZE, bit_slip_count);
        if (result.error_code != 0) {
            return result;
        }

        result.error_code = ReadXlPayloadLength(out_buffer, out_buffer_length, cursor, out_frame, result.dlc_corrected);
        if (result.error_code != 0) {
            return result;
        }
        cursor += XL_DATA_LENGTH_SIZE;

        result.error_code =
            CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor, out_buffer,
                                     out_buffer_length, cursor, XL_CONTROL_SIZE, bit_slip_count);
        if (result.error_code != 0) {
            return result;
        }

        result.error_code = ReadXlControl(out_buffer, out_buffer_length, cursor, out_frame);
        if (result.error_code != 0) {
            return result;
        }
        cursor += XL_CONTROL_SIZE;
    }

    const size_t can_id_size =
        out_frame->can_flags.IDE ? (CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE) : CAN_IDENTIFIER_SIZE;
    result.error_code = CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor,
                                                 out_buffer, out_buffer_length, cursor, can_id_size, bit_slip_count);
    if (result.error_code != 0) {
        return result;
    }

    result.error_code = ReadCanID(out_buffer, out_buffer_length, cursor, out_frame);
    if (result.error_code != 0) {
        return result;
    }
    cursor += can_id_size;

    if (out_frame->can_flags.TTL) {
        result.error_code =
            CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor, out_buffer,
                                     out_buffer_length, cursor, TIME_TO_LIVE_SIZE, bit_slip_count);
        if (result.error_code != 0) {
            return result;
        }

        result.error_code = ReadTTL(out_buffer, out_buffer_length, cursor, out_frame);
        if (result.error_code != 0) {
            return result;
        }
        cursor += TIME_TO_LIVE_SIZE;
    }

    const size_t remaining_bytes =
        out_frame->payload_length + out_frame->payload_length > 8 ? LONG_CRC_SIZE : SHORT_CRC_SIZE;
    result.error_code =
        CopyFromBitSlippedBuffer(source_buffer, source_buffer_length, source_buffer_offset + cursor, out_buffer,
                                 out_buffer_length, cursor, remaining_bytes, bit_slip_count);
    if (result.error_code != 0) {
        return result;
    }

    out_frame->payload_data = out_buffer + cursor;

    result.error_code = ValidateCRC(out_buffer, out_buffer_length, 0U, out_frame);
    if (result.error_code != 0) {
        return result;
    }

    return result;
}

/**
 * @brief Search for a SpIOpen frame preamble in a byte array buffer
 * @param buffer Pointer to the byte array buffer to find the preamble in
 * @param length Length of the byte array buffer
 * @param buffer_offset Offset of the first byte of the frame from the start of the buffer
 * @return Offset from the beginning of the buffer of the first byte in the buffer that matches the preamble or its
 * complement, or -1 if no preamble is found
 */
static size_t FindPreambleByte(const uint8_t* buffer, size_t length, size_t buffer_offset = 0) {
    if (buffer == nullptr) {
        return -1;
    }
    if (buffer_offset >= length) {
        return -1;
    }
    const void* standard_preamble_index = memchr(buffer + buffer_offset, PREAMBLE_BYTE, length - buffer_offset);
    const void* complement_preamble_index =
        memchr(buffer + buffer_offset, PREAMBLE_BYTE_COMPLEMENT, length - buffer_offset);
    if (standard_preamble_index == nullptr && complement_preamble_index == nullptr) {
        return -1;
    }
    if (standard_preamble_index == nullptr) {
        return static_cast<size_t>(static_cast<const uint8_t*>(complement_preamble_index) - buffer);
    }
    if (complement_preamble_index == nullptr) {
        return static_cast<size_t>(static_cast<const uint8_t*>(standard_preamble_index) - buffer);
    }
    return static_cast<size_t>(std::min(static_cast<const uint8_t*>(standard_preamble_index) - buffer,
                                        static_cast<const uint8_t*>(complement_preamble_index) - buffer));
}

/**
 * @brief Determine the number of bit slips that result in the earliest occurrence of the preamble in a byte array
 * buffer.
 * @param buffer Pointer to the byte array buffer
 * @param length Length of the byte array buffer
 * @param preamble_index Offset of the first byte identified as being either the preamble or its complement
 * @param out_bit_slip_count Reference to the number of bit slips detected. Positive for each bit into the preceding
 * byte that should be considered part of the preamble, to a maximum of 7. 0 indicates no bit slips.
 * @return 0 if the preamble was found and the bit slip count was determined, -1 if the preamble was not found, or a
 * frame parse error code if an error occurred.
 */
static int CountBitOffsetIntoPreviousByte(const uint8_t* buffer, size_t length, uint8_t& out_bit_offset,
                                          size_t preamble_index = 0) {
    if (buffer == nullptr) {
        return FRAME_PARSE_ERROR_INVALID_BUFFER_POINTER;
    }
    if (preamble_index + 1 >= length) {  // we will always need to search the next byte
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_FOR_PREAMBLE;
    }

    uint8_t index_byte = buffer[preamble_index];
    uint8_t next_byte = buffer[preamble_index + 1U];

    // The only way to return a success if the preambel_index is the first byte in the buffer is if the preamble is
    // perfectly aligned with our buffer (probably the most common case anyway)
    if (preamble_index == 0U) {
        if ((index_byte == PREAMBLE_BYTE) && (next_byte == PREAMBLE_BYTE)) {
            return 0;
        }
        return -1;  // searching algorithm will likely advance the cursor and inspect the next byte, with lookback to
                    // this first byte for a bit shift (second most common case)
    }
    // now we know that the previous byte is safe to read. The only question is, how many bits into the previous byte
    // does the preamble pattern extend?
    uint8_t previous_byte_pattern_match =
        buffer[preamble_index - 1U] & index_byte;  // remember that the index byte has either the preamble data or its
                                                   // complement, but that pattern should match across byte boundaries

    // bit slip will be either odd or even depending on whether the idnex has a true preamble or complement
    out_bit_offset = 6U;  // case where the true preamble is in the index position
    if (index_byte == PREAMBLE_BYTE_COMPLEMENT) {
        if (previous_byte_pattern_match & 1U == 0U) {
            return -1;  // this indicates that there is probably 7 bits of slip on the *next* buffer index.
        }
        out_bit_offset = 7U;  // now we know that we see a matching true preamble (not complement) one bit higher.
                              // So we can search just for the preamble.
    }
    uint8_t next_byte_pattern_match = next_byte & index_byte;
    uint8_t pattern_mask =
        (~0U) >> out_bit_offset;  // describes the bits of the previous byte that are part of the preamble pattern.
                                  // Will shift them out as we search for smaller and smaller shifts
    while (out_bit_offset >
           1U) {  // we already know that bit_slip of 1 is a match, but its our worst possible odd match.
        if (previous_byte_pattern_match & pattern_mask == pattern_mask &&
            next_byte_pattern_match & ~pattern_mask == ~pattern_mask) {
            return 0;
        }
        out_bit_offset -= 2U;
    }
    return 0;
}

FrameSearchResult FindFramePreamble(const uint8_t* buffer, size_t length, size_t buffer_offset,
                                    bool bit_slips_allowed) {
    FrameSearchResult result{};
    result.frame_start_offset = static_cast<size_t>(-1);
    result.bit_slip_count = 0;

    if (buffer == nullptr) {
        return result;
    }
    if (buffer_offset >= length) {
        return result;
    }

    const size_t preamble_index = FindPreambleByte(buffer, length, buffer_offset);
    result.frame_start_offset = preamble_index;

    if (bit_slips_allowed) {
        uint8_t bit_offset = 0;
        int error_code = CountBitOffsetIntoPreviousByte(buffer, length, bit_offset, preamble_index);
        if (error_code != 0) {
            return result;
        }
        if (bit_offset > 0) {  // bit offset function counts backwards from the "middle"byte, instead of forwards from
                               // the first partial byte
            result.bit_slip_count = static_cast<int8_t>(8U - bit_offset);
            result.frame_start_offset = preamble_index - 1u;
        }
    }

    return result;
}

}  // namespace spiopen::frame_reader
