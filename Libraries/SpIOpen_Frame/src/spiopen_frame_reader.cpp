/*
SpIOpen Frame Reader : Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame_reader.h"

#include <etl/byte_stream.h>
#include <etl/expected.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame_algorithms.h"

namespace spiopen::frame_reader {

using namespace spiopen::format;

namespace impl {

etl::expected<void, FrameParseError> ParseFormatHeader(const uint8_t high, const uint8_t low, Frame& frame,
                                                       bool& dlc_corrected, size_t& payload_len_out) {
    const uint16_t encoded_header = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
    const algorithms::Secded16DecodeResult decoded = algorithms::Secded16Decode11(encoded_header);
    if (decoded.uncorrectable) {
        return etl::unexpected(FrameParseError::FormatDlcCorrupted);
    }
    dlc_corrected = dlc_corrected || decoded.corrected;

    const uint16_t raw_header11 = decoded.data11;
    const uint8_t dlc_nibble = static_cast<uint8_t>(raw_header11 & HEADER_DLC_MASK);
    frame.can_flags.IDE = (raw_header11 & static_cast<uint16_t>(HEADER_IDE_MASK)) != 0U;
    frame.can_flags.FDF = (raw_header11 & static_cast<uint16_t>(HEADER_FDF_MASK)) != 0U;
    frame.can_flags.XLF = (raw_header11 & static_cast<uint16_t>(HEADER_XLF_MASK)) != 0U;
    frame.can_flags.TTL = (raw_header11 & static_cast<uint16_t>(HEADER_TTL_MASK)) != 0U;
    frame.can_flags.WA = ((raw_header11 >> 8U) & HEADER_WA_MASK) != 0U;

    payload_len_out = GetPayloadLengthFromDlc(dlc_nibble);
    return {};
}

etl::expected<void, FrameParseError> ValidatePreamble(etl::byte_stream_reader& stream) {
    auto b0 = stream.read<uint8_t>();
    if (!b0) {
        return etl::unexpected(FrameParseError::BufferTooShortForPreamble);
    }
    auto b1 = stream.read<uint8_t>();
    if (!b1) {
        return etl::unexpected(FrameParseError::BufferTooShortForPreamble);
    }
    if (*b0 != PREAMBLE_BYTE || *b1 != PREAMBLE_BYTE) {
        return etl::unexpected(FrameParseError::NoPreamble);
    }
    return {};
}

etl::expected<void, FrameParseError> ReadFormatHeader(etl::byte_stream_reader& stream, Frame& out_frame,
                                                      bool& dlc_corrected, size_t& payload_len_out) {
    auto hw = stream.read<uint16_t>();
    if (!hw) {
        return etl::unexpected(FrameParseError::BufferTooShortToDetermineLength);
    }
    const uint8_t low = static_cast<uint8_t>(*hw & 0xFFU);
    const uint8_t high = static_cast<uint8_t>(*hw >> 8U);

    auto parse_result = ParseFormatHeader(high, low, out_frame, dlc_corrected, payload_len_out);
    if (!parse_result) {
        return parse_result;
    }

#ifndef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (out_frame.can_flags.XLF) {
        return etl::unexpected(FrameParseError::CanXlNotSupported);
    }
#endif
    return {};
}

etl::expected<void, FrameParseError> ReadXlPayloadLength(etl::byte_stream_reader& stream, Frame& out_frame,
                                                         bool& dlc_corrected, size_t& payload_len_out) {
    auto enc = stream.read<uint16_t>();
    if (!enc) {
        return etl::unexpected(FrameParseError::BufferTooShortToDetermineLength);
    }
    const algorithms::Secded16DecodeResult decoded = algorithms::Secded16Decode11(*enc);
    if (decoded.uncorrectable) {
        return etl::unexpected(FrameParseError::FormatDlcCorrupted);
    }
    dlc_corrected = dlc_corrected || decoded.corrected;

    payload_len_out = static_cast<size_t>(decoded.data11);
    if (payload_len_out > MAX_XL_PAYLOAD_SIZE) {
        return etl::unexpected(FrameParseError::DlcInvalid);
    }
    return {};
}

etl::expected<void, FrameParseError> ReadXlControl(etl::byte_stream_reader& stream, Frame& out_frame) {
    auto pt = stream.read<uint8_t>();
    if (!pt) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    out_frame.xl_control.payload_type = *pt;

    auto vcid = stream.read<uint8_t>();
    if (!vcid) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    out_frame.xl_control.virtual_can_network_id = *vcid;

    auto addr = stream.read<uint32_t>();
    if (!addr) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    out_frame.xl_control.addressing_field = *addr;
    return {};
}

etl::expected<void, FrameParseError> ReadCanID(etl::byte_stream_reader& stream, Frame& out_frame) {
    auto cid_b0 = stream.read<uint8_t>();
    if (!cid_b0) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    auto cid_b1 = stream.read<uint8_t>();
    if (!cid_b1) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    out_frame.can_flags.RTR = (*cid_b0 & CID_RTR_MASK) != 0U;
    out_frame.can_flags.BRS = (*cid_b0 & CID_BRS_MASK) != 0U;
    out_frame.can_flags.ESI = (*cid_b0 & CID_ESI_MASK) != 0U;

    const uint8_t b0 = *cid_b0 & static_cast<uint8_t>(~(CID_RTR_MASK | CID_BRS_MASK | CID_ESI_MASK));
    if (out_frame.can_flags.IDE) {
        auto cid_b2 = stream.read<uint8_t>();
        if (!cid_b2) {
            return etl::unexpected(FrameParseError::BufferTooShortForHeader);
        }
        auto cid_b3 = stream.read<uint8_t>();
        if (!cid_b3) {
            return etl::unexpected(FrameParseError::BufferTooShortForHeader);
        }
        out_frame.can_identifier = (static_cast<uint32_t>(b0) << 24U) | (static_cast<uint32_t>(*cid_b1) << 16U) |
                                   (static_cast<uint32_t>(*cid_b2) << 8U) | static_cast<uint32_t>(*cid_b3);
    } else {
        out_frame.can_identifier = (static_cast<uint32_t>(b0) << 8U) | static_cast<uint32_t>(*cid_b1);
    }
    return {};
}

etl::expected<void, FrameParseError> ReadTTL(etl::byte_stream_reader& stream, Frame& out_frame) {
    auto ttl = stream.read<uint8_t>();
    if (!ttl) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    out_frame.time_to_live = *ttl;
    return {};
}

etl::expected<void, FrameParseError> ValidateCRC(etl::byte_stream_reader& stream, const Frame& frame,
                                                 const etl::span<const uint8_t>& crc_region) {
    size_t payload_length;
    if (!frame.TryGetPayloadSectionLength(payload_length)) {
        return etl::unexpected(FrameParseError::InvalidPayloadLength);
    }
    const size_t crc_size = GetCrcLengthFromPayloadLength(payload_length);
    if (crc_size == SHORT_CRC_SIZE) {
        auto received = stream.read<uint16_t>();
        if (!received) {
            return etl::unexpected(FrameParseError::BufferTooShortForPayload);
        }
        const uint16_t computed_crc = algorithms::ComputeCrc16(crc_region);
        if (computed_crc != *received) {
            return etl::unexpected(FrameParseError::CrcMismatch);
        }
    } else {
        auto received = stream.read<uint32_t>();
        if (!received) {
            return etl::unexpected(FrameParseError::BufferTooShortForPayload);
        }
        const uint32_t computed_crc = algorithms::ComputeCrc32(crc_region);
        if (computed_crc != *received) {
            return etl::unexpected(FrameParseError::CrcMismatch);
        }
    }
    return {};
}

//#TODO: use the ETL::bi_stream_X functions to make this more clear, and maybe more efficient
//@param bit_slip_count: positive for extra bits received (effectively a right shift of the data), to a maximum of 7. 0
// indicates no bit slips
// return true if the copy was successful, false otherwise.
// Dont use FrameParseError because the caller of this funciton can encoder more context on the failure (preamble,
// header, payload, crc, etc)
bool CopyFromBitSlippedBuffer(etl::byte_stream_reader& source, etl::byte_stream_writer& dest, size_t bytes_to_copy,
                              uint8_t bit_slip_count) {
    if (bit_slip_count > 7U) {
        return false;
    }
    if (dest.available_bytes() < bytes_to_copy) {
        return false;
    }
    if (bit_slip_count == 0U) {  // For byte aligned frames, we can just copy the data
        if (source.available_bytes() < bytes_to_copy) {
            return false;
        }
        std::memcpy(dest.free_data().data(), source.free_data().data(), bytes_to_copy);
        source.skip<uint8_t>(bytes_to_copy);
        dest.skip<uint8_t>(bytes_to_copy);
        return true;
    }
    // we need one extra byte from the source when compensating for bit slip
    if (source.available_bytes() <= bytes_to_copy) {
        return false;
    }
    uint8_t high = source.read_unchecked<uint8_t>();  // this is the "extra" byte that we read
    for (size_t i = 0U; i < bytes_to_copy; ++i) {
        uint8_t low = source.read_unchecked<uint8_t>();
        // correct for the effective right shift by left shifting the high byte back into alignment.
        // Then right shift the low byte by the opposite amount so we can OR the two together to get the final byte.
        const uint8_t out =
            static_cast<uint8_t>((static_cast<uint16_t>(high) << bit_slip_count) | (low >> (8U - bit_slip_count)));
        dest.write_unchecked(out);
        high = low;
    }
    source.restart(source.used_data().size() - 1U);  // move back one byte so a subsequent call can read a partial byte.
    return true;
}

}  // namespace impl

using namespace impl;

etl::expected<FrameReadResult, FrameParseError> ReadFrame(etl::byte_stream_reader& stream, Frame& out_frame) {
    FrameReadResult result{};
    result.dlc_corrected = false;

    out_frame.Reset();

    auto v = ValidatePreamble(stream);
    if (!v) {
        return etl::unexpected(v.error());
    }

    size_t start_position = stream.used_data().size();

    size_t payload_len = 0U;  // when reading from the wire, there is never any payload padding.
    auto hdr = ReadFormatHeader(stream, out_frame, result.dlc_corrected, payload_len);
    if (!hdr) {
        return etl::unexpected(hdr.error());
    }

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (out_frame.can_flags.XLF) {
        auto xl_len = ReadXlPayloadLength(stream, out_frame, result.dlc_corrected, payload_len);
        if (!xl_len) {
            return etl::unexpected(xl_len.error());
        }

        auto xl_ctrl = ReadXlControl(stream, out_frame);
        if (!xl_ctrl) {
            return etl::unexpected(xl_ctrl.error());
        }
    }
#endif

    auto cid = ReadCanID(stream, out_frame);
    if (!cid) {
        return etl::unexpected(cid.error());
    }

    if (out_frame.can_flags.TTL) {
        auto ttl = ReadTTL(stream, out_frame);
        if (!ttl) {
            return etl::unexpected(ttl.error());
        }
    }

    auto payload_data = stream.read<uint8_t>(payload_len);
    if (!payload_data) {
        return etl::unexpected(FrameParseError::BufferTooShortForPayload);
    }
    // it comes out of the reader as a const span, so we need to cast it to a mutable span
    out_frame.payload = etl::span<uint8_t>(const_cast<uint8_t*>((*payload_data).data()), (*payload_data).size());

    auto crc_region = stream.used_data().subspan(start_position);  // start position is marked after preamble
    auto crc =
        ValidateCRC(stream, out_frame,
                    etl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(crc_region.data()), crc_region.size()));
    if (!crc) {
        return etl::unexpected(crc.error());
    }

    return result;
}

etl::expected<FrameReadResult, FrameParseError> ReadAndCopyFrame(etl::byte_stream_reader& input_stream,
                                                                 etl::span<uint8_t> destination_buffer,
                                                                 Frame& out_frame, uint8_t bit_slip_count) {
    FrameReadResult result{};
    result.dlc_corrected = false;

    out_frame.Reset();

    etl::byte_stream_writer destination_stream_writer(destination_buffer,
                                                      etl::endian::big);  // used for transfer
    etl::byte_stream_reader destination_stream_reader(destination_buffer.data(), destination_buffer.size(),
                                                      etl::endian::big);  // used for parsing

    etl::expected<void, FrameParseError> frame_parse_result;

    // preamble - check it so we can exit early if this isnt even a frame
    if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, PREAMBLE_SIZE, bit_slip_count)) {
        return etl::unexpected(FrameParseError::BufferTooShortForPreamble);
    }
    frame_parse_result = ValidatePreamble(destination_stream_reader);
    if (!frame_parse_result) {
        return etl::unexpected(frame_parse_result.error());
    }

    // format header - after this we should know the full length of everything unless its a CAN-XL frame
    if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, FORMAT_HEADER_SIZE, bit_slip_count)) {
        return etl::unexpected(FrameParseError::BufferTooShortToDetermineLength);
    }
    size_t payload_len = 0U;  // this will hold the length of the payload in the wire, including payloa dpadding, but
                              // excluding frame padding.
    frame_parse_result = ReadFormatHeader(destination_stream_reader, out_frame, result.dlc_corrected, payload_len);
    if (!frame_parse_result) {
        return etl::unexpected(frame_parse_result.error());
    }

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    if (out_frame.can_flags.XLF) {
        // xl data length - we def know the total length after this
        if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, XL_DATA_LENGTH_SIZE, bit_slip_count)) {
            return etl::unexpected(FrameParseError::BufferTooShortToDetermineLength);
        }
        frame_parse_result =
            ReadXlPayloadLength(destination_stream_reader, out_frame, result.dlc_corrected, payload_len);
        if (!frame_parse_result) {
            return etl::unexpected(frame_parse_result.error());
        }

        // xl control
        if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, XL_CONTROL_SIZE, bit_slip_count)) {
            return etl::unexpected(FrameParseError::BufferTooShortForHeader);
        }
        frame_parse_result = ReadXlControl(destination_stream_reader, out_frame);
        if (!frame_parse_result) {
            return etl::unexpected(frame_parse_result.error());
        }
    }
#endif

    // can id
    const size_t can_id_size = out_frame.GetCanIdLength();
    if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, can_id_size, bit_slip_count)) {
        return etl::unexpected(FrameParseError::BufferTooShortForHeader);
    }
    frame_parse_result = ReadCanID(destination_stream_reader, out_frame);
    if (!frame_parse_result) {
        return etl::unexpected(frame_parse_result.error());
    }

    // ttl
    if (out_frame.can_flags.TTL) {
        if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, TIME_TO_LIVE_SIZE, bit_slip_count)) {
            return etl::unexpected(FrameParseError::BufferTooShortForHeader);
        }
        frame_parse_result = ReadTTL(destination_stream_reader, out_frame);
        if (!frame_parse_result) {
            return etl::unexpected(frame_parse_result.error());
        }
    }

    // payload
    auto payload_data = destination_stream_reader.free_data().subspan(0, payload_len);
    // will only become valid after the next copy, but it helps us to calculate how much to copy to finish off the
    // frame. We have to recast it to make it writable, even though the underlying buffer is writable.
    out_frame.payload =
        etl::span<uint8_t>(reinterpret_cast<uint8_t*>(const_cast<char*>(payload_data.data())), payload_data.size());
    size_t frame_length;
    if (!out_frame.TryGetFrameLength(frame_length)) {
        return etl::unexpected(FrameParseError::InvalidFrameLength);
    }
    size_t remaining_bytes = frame_length - (PREAMBLE_SIZE + out_frame.GetHeaderLength());
    // copy the rest of the frame
    if (!CopyFromBitSlippedBuffer(input_stream, destination_stream_writer, remaining_bytes, bit_slip_count)) {
        return etl::unexpected(FrameParseError::BufferTooShortForPayload);
    }
    destination_stream_reader.restart(frame_length -
                                      GetCrcLengthFromPayloadLength(payload_len));  // set up to read the crc
    auto crc_region = destination_stream_reader.used_data().subspan(PREAMBLE_SIZE);

    frame_parse_result =
        ValidateCRC(destination_stream_reader, out_frame,
                    etl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(crc_region.data()), crc_region.size()));
    if (!frame_parse_result) {
        return etl::unexpected(frame_parse_result.error());
    }

    return result;
}

namespace impl {

/**
 * @brief Search for a SpIOpen frame preamble in a byte array buffer
 * @param buffer Pointer to the byte array buffer to find the preamble in
 * @param offset Offset of the first byte of the frame from the start of the buffer
 * @param bit_slips_allowed True if bit slips are allowed during the search for the preamble (search for complement
 * preamble)
 * @return Offset from the beginning of the buffer of the first byte in the buffer that matches the preamble or its
 * complement, or a frame parse error code if an error occurred or no one-byte preamble was found.
 */
etl::expected<size_t, FrameParseError> FindNextPreambleByte(const etl::span<uint8_t>& buffer, size_t offset,
                                                            bool bit_slips_allowed) {
    if (offset >= buffer.size()) {
        return etl::unexpected(FrameParseError::BufferTooShortForPreamble);
    }
    const void* standard_preamble_index = memchr(buffer.data() + offset, PREAMBLE_BYTE, buffer.size() - offset);
    if (!bit_slips_allowed) {
        if (standard_preamble_index == nullptr) {
            return etl::unexpected(FrameParseError::NoPreamble);
        }
        return static_cast<size_t>(static_cast<const uint8_t*>(standard_preamble_index) - buffer.data());
    }

    // case where bit slips are allowed and we can also search for the complement preamble
    const void* complement_preamble_index =
        memchr(buffer.data() + offset, PREAMBLE_BYTE_COMPLEMENT, buffer.size() - offset);
    if (standard_preamble_index == nullptr && complement_preamble_index == nullptr) {
        return etl::unexpected(FrameParseError::NoPreamble);
    }
    if (standard_preamble_index == nullptr) {
        return static_cast<size_t>(static_cast<const uint8_t*>(complement_preamble_index) - buffer.data());
    }
    if (complement_preamble_index == nullptr) {
        return static_cast<size_t>(static_cast<const uint8_t*>(standard_preamble_index) - buffer.data());
    }
    return static_cast<size_t>(std::min(static_cast<const uint8_t*>(standard_preamble_index) - buffer.data(),
                                        static_cast<const uint8_t*>(complement_preamble_index) - buffer.data()));
}

/**
 * @brief Determine the number of bit slips that result in the earliest occurrence of the preamble in a byte array
 * buffer.
 * @param buffer Span of the byte array buffer
 * @param preamble_index Offset of the first byte identified as being either the preamble or its complement
 * @return The number of bits into the preceding byte that should be considered part of the preamble. Positive for each
 * bit into the preceding byte that should be considered part of the preamble, to a maximum of 7. 0 indicates no bit
 * slips. A frame parse error code if an error occurred or no full 2-byte preamble was found..
 */
etl::expected<uint8_t, FrameParseError> CountBitOffsetIntoPreviousByte(const etl::span<uint8_t>& buffer,
                                                                       size_t preamble_index) {
    if (preamble_index + 1U >= buffer.size()) {  // we will always need to search the next byte
        return etl::unexpected(FrameParseError::BufferTooShortForPreamble);
    }

    uint8_t index_byte = buffer[preamble_index];
    uint8_t next_byte = buffer[preamble_index + 1U];

    // The only way to return a success if the preambel_index is the first byte in the buffer is if the preamble is
    // perfectly aligned with our buffer (probably the most common case anyway)
    if (preamble_index == 0U) {
        if ((index_byte == PREAMBLE_BYTE) && (next_byte == PREAMBLE_BYTE)) {
            return 0U;  // no bit offsets
        }
        return etl::unexpected(FrameParseError::NoPreamble);  // searching algorithm will likely advance the cursor and
                                                              // inspect the next byte, with lookback to this first byte
    }
    // now we know that the previous byte is safe to read. The only question is, how many bits into the previous byte
    // does the preamble pattern extend?
    uint8_t previous_byte_pattern_match = ~(
        buffer[preamble_index - 1U] ^ index_byte);  // remember that the index byte has either the preamble data or its
                                                    // complement, but that pattern should match across byte boundaries

    // bit slip will be either odd or even depending on whether the idnex has a true preamble or complement
    uint8_t out_bit_offset = 6U;  // case where the true preamble is in the index position
    if (index_byte == PREAMBLE_BYTE_COMPLEMENT) {
        if ((previous_byte_pattern_match & 1U) == 0U) {
            return etl::unexpected(FrameParseError::NoPreamble);  // likely 7 bits of slip on the *next* buffer index
        }
        out_bit_offset =
            7U;  // now we know that we see a matching true preamble (not complement) one bit higher.
                 // So we can search just for the true preamble on odd slips (default is true preamble on even slips).
    }
    uint8_t next_byte_pattern_match = ~(next_byte ^ index_byte);
    uint8_t pattern_mask =
        (0xFFU) >> (8U - out_bit_offset);  // describes the bits of the previous byte that are part of the preamble
                                           // pattern. Will shift them out as we search for smaller and smaller shifts
    while (out_bit_offset > 1U) {  // we already know that bit_slip of 1 is a match if we are searching for odd slips,
        // but its our worst possible odd match.
        uint8_t pattern_mask_complement = (~pattern_mask);
        bool previous_byte_matches = (previous_byte_pattern_match & pattern_mask);
        bool next_byte_matches = (next_byte_pattern_match & pattern_mask_complement);
        if (((previous_byte_pattern_match & pattern_mask) == pattern_mask) &&
            ((next_byte_pattern_match & pattern_mask_complement) == pattern_mask_complement)) {
            return out_bit_offset;
        }
        out_bit_offset -= 2U;
        pattern_mask >>= 2U;
    }
    return out_bit_offset;
}

}  // namespace impl

FrameSearchResult FindNextFramePreamble(const etl::span<uint8_t>& buffer, size_t offset, bool bit_slips_allowed) {
    FrameSearchResult result{};
    result.valid_preamble_found = false;
    result.frame_start_offset = offset;  // use this as a working counter for candidate 2-byte preambles

    while (!result.valid_preamble_found) {
        auto preamble_index = FindNextPreambleByte(buffer, result.frame_start_offset, bit_slips_allowed);
        if (!preamble_index) {
            return result;  // give up, no preambles found in the rest of the buffer
        }
        if (bit_slips_allowed) {
            auto bit_result = CountBitOffsetIntoPreviousByte(buffer, *preamble_index);
            if (bit_result) {
                result.valid_preamble_found = true;
                result.bit_slip_count = static_cast<int8_t>(8U - *bit_result);
                result.frame_start_offset = *bit_result > 0 ? *preamble_index - 1u : *preamble_index;
                return result;
            }
        } else {
            if (*preamble_index + 1U < buffer.size() && buffer[*preamble_index + 1U] == PREAMBLE_BYTE) {
                result.valid_preamble_found = true;
                result.bit_slip_count = 0U;
                result.frame_start_offset = *preamble_index;
                return result;
            }
        }
        // no 2-byte preamble found. increment the search pointer and try again
        result.frame_start_offset = *preamble_index + 1u;
        continue;
    }
    return result;  // should never get here
}

}  // namespace spiopen::frame_reader
