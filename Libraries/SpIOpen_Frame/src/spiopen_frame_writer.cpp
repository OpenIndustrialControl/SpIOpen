/*
SpIOpen Frame Writer : Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame_writer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame_algorithms.h"
#include "spiopen_frame_format.h"

namespace spiopen::frame_writer {

using namespace spiopen::format;

// Returns 0 and (dlc_4bit, padding_bytes) for given payload_length when FDF is set.
// Returns FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH if payload_length > 64 (no valid CAN-FD DLC).
static int GetCanFdDlcAndPadding(size_t payload_length, uint8_t& dlc_out, size_t& padding_out) {
    if (payload_length <= 8U) {
        dlc_out = static_cast<uint8_t>(payload_length & 0x0FU);
        padding_out = 0U;
        return 0;
    }
    for (size_t i = 9; i < 16U; ++i) {
        if (payload_length <= format::CAN_FD_PAYLOAD_BY_DLC[i]) {
            dlc_out = static_cast<uint8_t>(i);
            padding_out = format::CAN_FD_PAYLOAD_BY_DLC[i] - payload_length;
            return 0;
        }
    }
    return FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH;
}

// Compute format header DLC nibble and payload padding from frame (for non-XL).
// Returns 0 on success, FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH if length is invalid for frame type.
static int GetDlcAndPayloadPadding(const Frame* frame, uint8_t& dlc_low_nibble, size_t& payload_padding) {
    const bool fdf = frame->can_flags.FDF;
    const bool xlf = frame->can_flags.XLF;
    const size_t len = frame->payload_length;

    if (xlf) {
        dlc_low_nibble = 0U;
        payload_padding = 0U;
        return 0;
    }
    if (!fdf) {
        if (len > MAX_CC_PAYLOAD_SIZE) {
            return FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH;
        }
        dlc_low_nibble = static_cast<uint8_t>(len & 0x0FU);
        payload_padding = 0U;
        return 0;
    }
    return GetCanFdDlcAndPadding(len, dlc_low_nibble, payload_padding);
}

// Checks that the frame is internally valid (data length, etc) and that the buffer can hol the frame.
// Returns 0 on success, FRAME_WRITE_ERROR_* on failure.
static int ValidateFrameAndBuffer(const Frame* frame, uint8_t* buffer, size_t buffer_length, size_t required_length) {
    if (frame == nullptr) {
        return FRAME_WRITE_ERROR_INVALID_FRAME_POINTER;
    }
    if (buffer == nullptr) {
        return FRAME_WRITE_ERROR_INVALID_BUFFER_POINTER;
    }
    const size_t payload_len = frame->payload_length;
    const uint8_t* payload_data = frame->payload_data;
    if (payload_len > 0U && payload_data == nullptr) {
        return FRAME_WRITE_ERROR_INVALID_PAYLOAD_POINTER;
    }
    const bool fdf = frame->can_flags.FDF;
    const bool xlf = frame->can_flags.XLF;
    if (!xlf && !fdf && payload_len > MAX_CC_PAYLOAD_SIZE) {
        return FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH;
    }
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    if (!xlf && fdf && payload_len > MAX_FD_PAYLOAD_SIZE) {
        return FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH;
    }
#else
    if (fdf) {
        return FRAME_WRITE_ERROR_CANFD_NOT_SUPPORTED;
    }
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (xlf && payload_len > MAX_XL_PAYLOAD_SIZE) {
        return FRAME_WRITE_ERROR_INVALID_PAYLOAD_LENGTH;
    }
#else
    if (xlf) {
        return FRAME_WRITE_ERROR_CANXL_NOT_SUPPORTED;
    }
#endif
    if (buffer_length < required_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    return 0;
}

// --- Helper: Preamble ---
static int WritePreamble(uint8_t* buffer, size_t buffer_length, size_t& offset) {
    if (offset + PREAMBLE_SIZE > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    buffer[offset++] = PREAMBLE_BYTE;
    buffer[offset++] = PREAMBLE_BYTE;
    return 0;
}

// --- Helper: Format header (2 bytes). Packed 11-bit layout without SECDED; high byte first. ---
static int WriteFormatHeader(const Frame* frame, uint8_t dlc_low_nibble, uint8_t* buffer, size_t buffer_length,
                             size_t& offset) {
    if (offset + FORMAT_HEADER_SIZE > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    const uint8_t low = static_cast<uint8_t>(
        (dlc_low_nibble & HEADER_DLC_MASK) | (frame->can_flags.IDE ? HEADER_IDE_MASK : 0U) |
        (frame->can_flags.FDF ? HEADER_FDF_MASK : 0U) | (frame->can_flags.XLF ? HEADER_XLF_MASK : 0U) |
        (frame->can_flags.TTL ? HEADER_TTL_MASK : 0U));
    const uint8_t high = frame->can_flags.WA ? HEADER_WA_MASK : 0U;

    const uint16_t raw_header11 =
        static_cast<uint16_t>(low) | static_cast<uint16_t>((static_cast<uint16_t>(high & HEADER_WA_MASK)) << 8U);
    const uint16_t encoded_header = algorithms::Secded16Encode11(raw_header11);
    buffer[offset++] = static_cast<uint8_t>((encoded_header >> 8U) & 0xFFU);
    buffer[offset++] = static_cast<uint8_t>(encoded_header & 0xFFU);
    return 0;
}

// --- Helper: CAN Identifier (2 or 4 bytes, MSB first; RTR/BRS/ESI in top bits of first byte) ---
static int WriteCanIdentifier(const Frame* frame, uint8_t* buffer, size_t buffer_length, size_t& offset) {
    const size_t cid_size =
        frame->can_flags.IDE ? (CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE) : CAN_IDENTIFIER_SIZE;
    if (offset + cid_size > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    uint32_t id = frame->can_identifier;
    const uint8_t high_byte_flags = (frame->can_flags.RTR ? CID_RTR_MASK : 0U) |
                                    (frame->can_flags.BRS ? CID_BRS_MASK : 0U) |
                                    (frame->can_flags.ESI ? CID_ESI_MASK : 0U);

    if (frame->can_flags.IDE) {
        buffer[offset++] = static_cast<uint8_t>((id >> 24U) & 0xFFU) | high_byte_flags;
        buffer[offset++] = static_cast<uint8_t>((id >> 16U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>((id >> 8U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>((id)&0xFFU);
    } else {
        buffer[offset++] = static_cast<uint8_t>((id >> 8U) & 0xFFU) | high_byte_flags;
        buffer[offset++] = static_cast<uint8_t>((id)&0xFFU);
    }
    return 0;
}

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
// --- Helper: XL Control (8 bytes). Multi-byte fields MSB first on wire. ---
static int WriteXlControl(const Frame* frame, uint8_t* buffer, size_t buffer_length, size_t& offset) {
    if (offset + XL_CONTROL_SIZE > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    Frame::XLControl xl = frame->xl_control;
    const uint16_t encoded_xl_dlc =
        algorithms::Secded16Encode11(static_cast<uint16_t>(frame->payload_length & 0x07FFU));
    buffer[offset++] = static_cast<uint8_t>((encoded_xl_dlc >> 8U) & 0xFFU);
    buffer[offset++] = static_cast<uint8_t>(encoded_xl_dlc & 0xFFU);
    buffer[offset++] = xl.payload_type;
    buffer[offset++] = xl.virtual_can_network_id;
    buffer[offset++] = static_cast<uint8_t>((xl.addressing_field >> 24U) & 0xFFU);
    buffer[offset++] = static_cast<uint8_t>((xl.addressing_field >> 16U) & 0xFFU);
    buffer[offset++] = static_cast<uint8_t>((xl.addressing_field >> 8U) & 0xFFU);
    buffer[offset++] = static_cast<uint8_t>(xl.addressing_field & 0xFFU);
    return 0;
}
#endif

// --- Helper: Time to Live (1 byte if TTL flag set) ---
static int WriteTimeToLive(const Frame* frame, uint8_t* buffer, size_t buffer_length, size_t& offset) {
    if (!frame->can_flags.TTL) {
        return 0;
    }
    if (offset + TIME_TO_LIVE_SIZE > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    buffer[offset++] = frame->time_to_live;
    return 0;
}

// --- Helper: Payload + DLC padding ---
static int WritePayload(const Frame* frame, size_t payload_padding, uint8_t* buffer, size_t buffer_length,
                        size_t& offset, size_t& payload_padding_added) {
    const size_t len = frame->payload_length;
    const uint8_t* src = frame->payload_data;
    if (len > 0U && src == nullptr) {
        return FRAME_WRITE_ERROR_INVALID_PAYLOAD_POINTER;
    }
    size_t total = len + payload_padding;
    if (offset + total > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    if (len > 0U) {
        std::memcpy(buffer + offset, src, len);
        offset += len;
    }
    payload_padding_added = payload_padding;
    for (size_t i = 0; i < payload_padding; ++i) {
        buffer[offset++] = 0U;
    }
    return 0;
}

// --- Helper: CRC over [crc_region_start, crc_region_start + crc_region_length) ---
static int WriteCrc(const uint8_t* crc_region_start, size_t crc_region_length, bool use_crc32, uint8_t* buffer,
                    size_t buffer_length, size_t& offset) {
    const size_t crc_size = use_crc32 ? LONG_CRC_SIZE : SHORT_CRC_SIZE;
    if (offset + crc_size > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    if (use_crc32) {
        const uint32_t v = algorithms::ComputeCrc32(crc_region_start, crc_region_length);
        buffer[offset++] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>(v & 0xFFU);
    } else {
        const uint16_t v = algorithms::ComputeCrc16(crc_region_start, crc_region_length);
        buffer[offset++] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
        buffer[offset++] = static_cast<uint8_t>(v & 0xFFU);
    }
    return 0;
}

// --- Helper: Frame padding for word alignment ---
static int WriteFramePadding(bool word_align, size_t current_length, uint8_t* buffer, size_t buffer_length,
                             size_t& offset, size_t& frame_padding_added) {
    frame_padding_added = 0U;
    if (!word_align || (current_length & 1U) == 0U) {
        return 0;
    }
    if (offset + MAX_PADDING_SIZE > buffer_length) {
        return FRAME_WRITE_ERROR_BUFFER_TOO_SHORT;
    }
    buffer[offset++] = 0U;
    frame_padding_added = MAX_PADDING_SIZE;
    return 0;
}

// --- Public API ---
FrameWriteResult WriteFrame(const Frame* frame, uint8_t* buffer, size_t buffer_length) {
    FrameWriteResult result = {};
    result.error_code = 0;
    result.payload_padding_added = 0;
    result.frame_padding_added = 0;
    result.total_length = 0;

    if (frame == nullptr) {
        result.error_code = FRAME_WRITE_ERROR_INVALID_FRAME_POINTER;
        return result;
    }
    if (buffer == nullptr) {
        result.error_code = FRAME_WRITE_ERROR_INVALID_BUFFER_POINTER;
        return result;
    }

    const size_t required_length = frame->GetFrameLength();
    int err = ValidateFrameAndBuffer(frame, buffer, buffer_length, required_length);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    uint8_t dlc_low_nibble = 0;
    size_t payload_padding = 0;
    err = GetDlcAndPayloadPadding(frame, dlc_low_nibble, payload_padding);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    size_t offset = 0;

    err = WritePreamble(buffer, buffer_length, offset);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    // Save offset of start of CRC region for later. Everything except preambel is included in the CRC region.
    const size_t crc_region_start_offset = offset;

    err = WriteFormatHeader(frame, dlc_low_nibble, buffer, buffer_length, offset);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (frame->can_flags.XLF) {
        err = WriteXlControl(frame, buffer, buffer_length, offset);
        if (err != 0) {
            result.error_code = err;
            return result;
        }
    }
#endif

    err = WriteCanIdentifier(frame, buffer, buffer_length, offset);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    err = WriteTimeToLive(frame, buffer, buffer_length, offset);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    err = WritePayload(frame, payload_padding, buffer, buffer_length, offset, result.payload_padding_added);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    // Frame padding (before CRC per spec: Data, [Padding], CRC)
    err = WriteFramePadding(frame->can_flags.WA, offset, buffer, buffer_length, offset, result.frame_padding_added);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    const size_t crc_region_length = offset - crc_region_start_offset;
    const bool use_crc32 = frame->payload_length > MAX_CC_PAYLOAD_SIZE;

    err = WriteCrc(buffer + crc_region_start_offset, crc_region_length, use_crc32, buffer, buffer_length, offset);
    if (err != 0) {
        result.error_code = err;
        return result;
    }

    result.total_length = offset;
    return result;
}

}  // namespace spiopen::frame_writer
