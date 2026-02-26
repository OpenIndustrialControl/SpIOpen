/*
SpIOpen Frame Reader : Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame_reader.h"

#include <cstddef>
#include <cstdint>

#include "etl/crc16_ccitt.h"
#include "etl/crc32_mpeg2.h"

namespace spiopen::FrameReader {

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

static inline void ParseFormatHeader(const uint8_t high, const uint8_t low, Frame& frame) {
    const uint8_t dlc_nibble = static_cast<uint8_t>(low & HEADER_DLC_MASK);
    frame.can_flags.IDE = (low & HEADER_IDE_MASK) != 0U;
    frame.can_flags.FDF = (low & HEADER_FDF_MASK) != 0U;
    frame.can_flags.XLF = (low & HEADER_XLF_MASK) != 0U;
    frame.can_flags.TTL = (low & HEADER_TTL_MASK) != 0U;
    frame.can_flags.WA = (high & HEADER_WA_MASK) != 0U;

    // For non-XL frames, payload length is derived from DLC here.
    // For XL, this value will be zero, and will be replaced by the XL payload length field later.
    frame.payload_length = DecodePayloadLength(dlc_nibble, frame.can_flags);
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

static int ReadFormatHeader(const uint8_t* buffer, const size_t buffer_length, const size_t cursor, Frame* out_frame) {
    if (!CanRead(cursor, FORMAT_HEADER_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH;
    }

    ParseFormatHeader(buffer[cursor], buffer[cursor + 1U], *out_frame);

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

static int ReadXlPayloadLength(const uint8_t* buffer, const size_t buffer_length, const size_t cursor,
                               Frame* out_frame) {
    // Assume that this is an XL frame if the function is being called.
    // The caller assumes that this function will read two bytes.

    // In XL, the first two bytes immediately after the format header are payload length.
    if (!CanRead(cursor, XL_DATA_LENGTH_SIZE, buffer_length)) {
        return FRAME_PARSE_ERROR_BUFFER_TOO_SHORT_TO_DETERMINE_LENGTH;
    }

    out_frame->payload_length =
        static_cast<size_t>((static_cast<uint16_t>(buffer[cursor]) << 8U) | static_cast<uint16_t>(buffer[cursor + 1U]));
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
        etl::crc16_ccitt crc;
        crc.add(buffer + crc_region_start, buffer + crc_region_start + crc_region_len);
        if (static_cast<uint16_t>(crc.value()) != received_crc) {
            return FRAME_PARSE_ERROR_CRC_MISMATCH;
        }
    } else {
        const uint32_t received_crc =
            (static_cast<uint32_t>(buffer[cursor]) << 24U) | (static_cast<uint32_t>(buffer[cursor + 1U]) << 16U) |
            (static_cast<uint32_t>(buffer[cursor + 2U]) << 8U) | static_cast<uint32_t>(buffer[cursor + 3U]);
        etl::crc32_mpeg2 crc;
        crc.add(buffer + crc_region_start, buffer + crc_region_start + crc_region_len);
        if (crc.value() != received_crc) {
            return FRAME_PARSE_ERROR_CRC_MISMATCH;
        }
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

    result.error_code = ReadFormatHeader(buffer, buffer_length, cursor, out_frame);
    if (result.error_code != 0) {
        return result;
    }
    cursor += FORMAT_HEADER_SIZE;

    if (out_frame->can_flags.XLF) {
        result.error_code = ReadXlPayloadLength(buffer, buffer_length, cursor, out_frame);
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

}  // namespace spiopen::FrameReader
