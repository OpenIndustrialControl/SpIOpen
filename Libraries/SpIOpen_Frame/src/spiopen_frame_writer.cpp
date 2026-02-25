/*
SpIOpen Frame Writer : Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame_writer.h"

#include <etl/byte_stream.h>
#include <etl/expected.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame_algorithms.h"
#include "spiopen_frame_format.h"

namespace spiopen::frame_writer {

using namespace spiopen::format;

namespace impl {

// Checks that the frame is internally valid (data length, etc) and that the buffer can hold the frame.
etl::expected<void, FrameWriteError> ValidateFrame(etl::byte_stream_writer& stream, const Frame& frame) {
    size_t payload_len;
    if (!frame.TryGetPayloadSectionLength(payload_len)) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
    size_t frame_len;
    if (!frame.TryGetFrameLength(frame_len)) {
        return etl::unexpected(FrameWriteError::InvalidFrameLength);
    }
    if (stream.available_bytes() < frame_len) {
        return etl::unexpected(FrameWriteError::BufferTooShort);
    }

    const bool fdf = frame.can_flags.FDF;
    const bool xlf = frame.can_flags.XLF;
    if (!xlf && !fdf && payload_len > MAX_CC_PAYLOAD_SIZE) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    if (!xlf && fdf && payload_len > MAX_FD_PAYLOAD_SIZE) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
#else
    if (fdf) {
        return etl::unexpected(FrameWriteError::CanFdNotSupported);
    }
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (xlf && payload_len > MAX_XL_PAYLOAD_SIZE) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
#else
    if (xlf) {
        return etl::unexpected(FrameWriteError::CanXlNotSupported);
    }
#endif

    return {};
}

/**
 * @brief Write the preamble bytes to the stream (big-endian word 0xAAAA).
 */
etl::expected<void, FrameWriteError> WritePreamble(etl::byte_stream_writer& stream) {
    return stream.write(static_cast<uint16_t>(PREAMBLE_WORD)) ? etl::expected<void, FrameWriteError>()
                                                              : etl::unexpected(FrameWriteError::BufferTooShort);
}

/**
 * @brief Write the encoded format header (11-bit SECDED) to the stream as big-endian uint16_t.
 */
etl::expected<void, FrameWriteError> WriteFormatHeader(etl::byte_stream_writer& stream, const Frame& frame) {
    uint8_t dlc_low_nibble = 0;
    if (!TryGetDlcFromPayloadLength(frame.payload.size(), dlc_low_nibble)) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
    const uint8_t low = static_cast<uint8_t>(
        (dlc_low_nibble & HEADER_DLC_MASK) | (frame.can_flags.IDE ? HEADER_IDE_MASK : 0U) |
        (frame.can_flags.FDF ? HEADER_FDF_MASK : 0U) | (frame.can_flags.XLF ? HEADER_XLF_MASK : 0U) |
        (frame.can_flags.TTL ? HEADER_TTL_MASK : 0U));
    const uint8_t high = frame.can_flags.WA ? HEADER_WA_MASK : 0U;

    const uint16_t raw_header11 =
        static_cast<uint16_t>(low) | static_cast<uint16_t>((static_cast<uint16_t>(high & HEADER_WA_MASK)) << 8U);
    const uint16_t encoded_header = algorithms::Secded16Encode11(raw_header11);

    return stream.write(encoded_header) ? etl::expected<void, FrameWriteError>()
                                        : etl::unexpected(FrameWriteError::BufferTooShort);
}

/**
 * @brief Write the CAN identifier (standard or extended) and flag bits to the stream (big-endian).
 */
etl::expected<void, FrameWriteError> WriteCanIdentifier(etl::byte_stream_writer& stream, const Frame& frame) {
    const size_t cid_size = frame.GetCanIdLength();
    uint32_t id32 = frame.can_identifier;
    const uint8_t high_byte_flags = (frame.can_flags.RTR ? CID_RTR_MASK : 0U) |
                                    (frame.can_flags.BRS ? CID_BRS_MASK : 0U) |
                                    (frame.can_flags.ESI ? CID_ESI_MASK : 0U);

    if (frame.can_flags.IDE) {
        id32 |= static_cast<uint32_t>(high_byte_flags) << 24U;
        return stream.write(id32) ? etl::expected<void, FrameWriteError>()
                                  : etl::unexpected(FrameWriteError::BufferTooShort);
    } else {
        const uint16_t id16 = static_cast<uint16_t>(id32) | (static_cast<uint16_t>(high_byte_flags) << 8U);
        return stream.write(id16) ? etl::expected<void, FrameWriteError>()
                                  : etl::unexpected(FrameWriteError::BufferTooShort);
    }
}

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
/**
 * @brief Write the XL data length (SECDED) and control field to the stream (big-endian multi-byte values).
 */
etl::expected<void, FrameWriteError> WriteXlDataAndControl(etl::byte_stream_writer& stream, const Frame& frame) {
    const uint16_t encoded_xl_dlc = algorithms::Secded16Encode11(static_cast<uint16_t>(frame.payload.size() & 0x07FFU));
    bool wrote_all = true;
    // short circuit evaluation to write all the bytes
    return (stream.write(encoded_xl_dlc) && stream.write(frame.xl_control.payload_type) &&
            stream.write(frame.xl_control.virtual_can_network_id) && stream.write(frame.xl_control.addressing_field))
               ? etl::expected<void, FrameWriteError>()
               : etl::unexpected(FrameWriteError::BufferTooShort);
}
#endif

/**
 * @brief Write the Time-To-Live byte to the stream if the TTL flag is set.
 */
etl::expected<void, FrameWriteError> WriteTimeToLive(etl::byte_stream_writer& stream, const Frame& frame) {
    if (!frame.can_flags.TTL) {
        return {};
    }
    return stream.write(frame.time_to_live) ? etl::expected<void, FrameWriteError>()
                                            : etl::unexpected(FrameWriteError::BufferTooShort);
}

/**
 * @brief Write the payload bytes and any required DLC padding to the stream.
 */
etl::expected<void, FrameWriteError> WritePayload(etl::byte_stream_writer& stream, const Frame& frame) {
    const etl::span<uint8_t> payload = frame.payload;
    size_t section_length;
    if (!frame.TryGetPayloadSectionLength(section_length)) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }

    if (payload.size() > 0U) {
        if (!stream.write(payload)) {
            return etl::unexpected(FrameWriteError::BufferTooShort);
        }
    }
    for (size_t i = payload.size(); i < section_length; ++i) {
        if (!stream.write(static_cast<uint8_t>(0U))) {
            return etl::unexpected(FrameWriteError::BufferTooShort);
        }
    }
    return {};
}

/**
 * @brief Write word-alignment padding byte to the stream if WA flag is set and current length is odd.
 */
etl::expected<void, FrameWriteError> WriteFramePadding(etl::byte_stream_writer& stream, const Frame& frame) {
    if (!frame.can_flags.WA) {
        return {};
    }
    const size_t current_length = stream.size_bytes() - PREAMBLE_SIZE;
    if (etl::is_even(current_length)) {
        return {};
    }
    return stream.write(static_cast<uint8_t>(0U)) ? etl::expected<void, FrameWriteError>()
                                                  : etl::unexpected(FrameWriteError::BufferTooShort);
}

/**
 * @brief Write the CRC (16 or 32-bit) over the stream content after the preamble into the stream (big-endian).
 */
etl::expected<void, FrameWriteError> WriteCrc(etl::byte_stream_writer& stream, const Frame& frame,
                                              const etl::span<const uint8_t>& crc_region) {
    size_t section_length;
    if (!frame.TryGetPayloadSectionLength(section_length)) {
        return etl::unexpected(FrameWriteError::InvalidPayloadLength);
    }
    const size_t crc_size = GetCrcLengthFromPayloadLength(section_length);

    if (crc_size == LONG_CRC_SIZE) {
        const uint32_t v = algorithms::ComputeCrc32(crc_region);
        return stream.write(v) ? etl::expected<void, FrameWriteError>()
                               : etl::unexpected(FrameWriteError::BufferTooShort);
    }
    if (crc_size == SHORT_CRC_SIZE) {
        const uint16_t v = algorithms::ComputeCrc16(crc_region);
        return stream.write(v) ? etl::expected<void, FrameWriteError>()
                               : etl::unexpected(FrameWriteError::BufferTooShort);
    }
    return etl::unexpected(FrameWriteError::InvalidPayloadLength);  // should never happen
}

}  // namespace impl

using namespace spiopen::frame_writer::impl;

// --- Public API ---
etl::expected<void, FrameWriteError> WriteFrame(etl::byte_stream_writer& stream, const Frame& frame) {
    auto valid = ValidateFrame(stream, frame);
    if (!valid) {
        return etl::unexpected(valid.error());
    }

    size_t start_position = stream.used_data().size();

    auto pre = WritePreamble(stream);
    if (!pre) {
        return etl::unexpected(pre.error());
    }

    auto hdr = WriteFormatHeader(stream, frame);
    if (!hdr) {
        return etl::unexpected(hdr.error());
    }

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (frame.can_flags.XLF) {
        auto xl = WriteXlDataAndControl(stream, frame);
        if (!xl) {
            return etl::unexpected(xl.error());
        }
    }
#endif

    auto cid = WriteCanIdentifier(stream, frame);
    if (!cid) {
        return etl::unexpected(cid.error());
    }

    auto ttl = WriteTimeToLive(stream, frame);
    if (!ttl) {
        return etl::unexpected(ttl.error());
    }

    auto pay = WritePayload(stream, frame);
    if (!pay) {
        return etl::unexpected(pay.error());
    }

    auto pad = WriteFramePadding(stream, frame);
    if (!pad) {
        return etl::unexpected(pad.error());
    }
    auto crc_region = stream.used_data().subspan(PREAMBLE_SIZE + start_position);
    auto crc =
        WriteCrc(stream, frame,
                 etl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(crc_region.data()), crc_region.size()));
    if (!crc) {
        return etl::unexpected(crc.error());
    }

    return {};
}

}  // namespace spiopen::frame_writer
