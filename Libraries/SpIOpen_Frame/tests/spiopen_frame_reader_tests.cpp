#include <etl/byte_stream.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame.h"
#include "spiopen_frame_algorithms.h"
#include "spiopen_frame_format.h"
#include "spiopen_frame_reader.h"

using namespace spiopen;
using namespace spiopen::format;
using namespace spiopen::frame_reader;
using namespace spiopen::frame_reader::impl;

static uint16_t EncodeFormatHeader11(uint8_t dlc_nibble, bool IDE, bool FDF, bool XLF, bool TTL, bool WA) {
    const uint16_t low = (dlc_nibble & HEADER_DLC_MASK) | (IDE ? HEADER_IDE_MASK : 0U) | (FDF ? HEADER_FDF_MASK : 0U) |
                         (XLF ? HEADER_XLF_MASK : 0U) | (TTL ? HEADER_TTL_MASK : 0U);
    const uint16_t high = WA ? HEADER_WA_MASK : 0U;
    return algorithms::Secded16Encode11(low | (high << 8U));
}

TEST(SpIOpen_FrameReader, ParseFormatHeader) {
    {
        // CC frame: DLC=2, no flags
        const uint16_t encoded = EncodeFormatHeader11(2, false, false, false, false, false);
        const uint8_t high = static_cast<uint8_t>(encoded >> 8U);
        const uint8_t low = static_cast<uint8_t>(encoded & 0xFFU);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 0;
        auto ret = ParseFormatHeader(high, low, frame, dlc_corrected, payload_len);
        ASSERT_TRUE(ret) << "ParseFormatHeader should succeed for valid CC header";
        EXPECT_EQ(payload_len, 2U) << "payload_len should be 2 for DLC=2 CC header";
        EXPECT_FALSE(frame.can_flags.IDE) << "IDE should be clear for CC header with no IDE";
        EXPECT_FALSE(frame.can_flags.FDF) << "FDF should be clear for CC header";
        EXPECT_FALSE(frame.can_flags.XLF) << "XLF should be clear for CC header";
        EXPECT_FALSE(frame.can_flags.TTL) << "TTL should be clear for CC header";
        EXPECT_FALSE(frame.can_flags.WA) << "WA should be clear for CC header with no WA";
    }

    {
        // Header with IDE, WA set; DLC=0
        const uint16_t encoded = EncodeFormatHeader11(0, true, false, false, false, true);
        const uint8_t high = static_cast<uint8_t>(encoded >> 8U);
        const uint8_t low = static_cast<uint8_t>(encoded & 0xFFU);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 99;
        auto ret = ParseFormatHeader(high, low, frame, dlc_corrected, payload_len);
        ASSERT_TRUE(ret) << "ParseFormatHeader should succeed for header with IDE and WA set";
        EXPECT_EQ(payload_len, 0U) << "payload_len should be 0 for DLC=0";
        EXPECT_TRUE(frame.can_flags.IDE) << "IDE should be set from header";
        EXPECT_TRUE(frame.can_flags.WA) << "WA should be set from header";
    }

    {
        // Corrupted (invalid SECDED): flip two bits so uncorrectable
        const uint16_t encoded = EncodeFormatHeader11(1, false, false, false, false, false);
        const uint16_t corrupted = encoded ^ 0x0300U;  // two bit flips
        const uint8_t high = static_cast<uint8_t>(corrupted >> 8U);
        const uint8_t low = static_cast<uint8_t>(corrupted & 0xFFU);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 0;
        auto ret = ParseFormatHeader(high, low, frame, dlc_corrected, payload_len);
        EXPECT_FALSE(ret) << "ParseFormatHeader should fail for uncorrectable SECDED corruption";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::FormatDlcCorrupted) << "error should be FormatDlcCorrupted";
        }
    }
}

TEST(SpIOpen_FrameReader, ValidatePreamble) {
    {
        const uint8_t buffer[] = {PREAMBLE_BYTE, PREAMBLE_BYTE};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        auto ret = ValidatePreamble(stream);
        ASSERT_TRUE(ret) << "ValidatePreamble should succeed for valid preamble";
    }

    {
        const uint8_t buffer[] = {PREAMBLE_BYTE};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        auto ret = ValidatePreamble(stream);
        EXPECT_FALSE(ret) << "ValidatePreamble should fail when stream has only one byte";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForPreamble)
                << "error should be BufferTooShortForPreamble";
        }
    }

    {
        const uint8_t buffer[] = {0x00, PREAMBLE_BYTE};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        auto ret = ValidatePreamble(stream);
        EXPECT_FALSE(ret) << "ValidatePreamble should fail when first byte is not preamble";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::NoPreamble) << "error should be NoPreamble";
        }
    }
}

TEST(SpIOpen_FrameReader,
     ReadFormatHeader){{const uint16_t encoded = EncodeFormatHeader11(3, false, false, false, false, false);
uint8_t buffer[4] = {0};
buffer[0] = static_cast<uint8_t>(encoded >> 8U);
buffer[1] = static_cast<uint8_t>(encoded & 0xFFU);
etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
Frame frame{};
bool dlc_corrected = false;
size_t payload_len = 0;
auto ret = ReadFormatHeader(stream, frame, dlc_corrected, payload_len);
ASSERT_TRUE(ret) << "ReadFormatHeader should succeed with sufficient buffer";
EXPECT_EQ(payload_len, 3U) << "payload_len should be 3 for DLC=3";
}

{
    uint8_t buffer[1] = {0};
    etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
    Frame frame{};
    bool dlc_corrected = false;
    size_t payload_len = 0;
    auto ret = ReadFormatHeader(stream, frame, dlc_corrected, payload_len);
    EXPECT_FALSE(ret) << "ReadFormatHeader should fail when stream cannot provide uint16_t";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortToDetermineLength)
            << "error should be BufferTooShortToDetermineLength";
    }
}

#ifndef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
{
    const uint16_t encoded = EncodeFormatHeader11(0, false, false, true, false, false);
    uint8_t buffer[4] = {0};
    buffer[0] = static_cast<uint8_t>(encoded >> 8U);
    buffer[1] = static_cast<uint8_t>(encoded & 0xFFU);
    etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
    Frame frame{};
    bool dlc_corrected = false;
    size_t payload_len = 0;
    auto ret = ReadFormatHeader(stream, frame, dlc_corrected, payload_len);
    EXPECT_FALSE(ret) << "ReadFormatHeader should fail when XL not enabled and header has XLF set";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameParseError::CanXlNotSupported) << "error should be CanXlNotSupported";
    }
}
#endif
}

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
TEST(SpIOpen_FrameReader, ReadXlPayloadLength) {
    {
        const uint16_t encoded = algorithms::Secded16Encode11(10);
        uint8_t buffer[4] = {0};
        buffer[0] = static_cast<uint8_t>(encoded >> 8U);
        buffer[1] = static_cast<uint8_t>(encoded & 0xFFU);
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 0;
        auto ret = ReadXlPayloadLength(stream, frame, dlc_corrected, payload_len);
        ASSERT_TRUE(ret) << "ReadXlPayloadLength should succeed for valid encoded length 10";
        EXPECT_EQ(payload_len, 10U) << "payload_len_out should be 10";
    }

    {
        uint8_t buffer[1] = {0};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 0;
        auto ret = ReadXlPayloadLength(stream, frame, dlc_corrected, payload_len);
        EXPECT_FALSE(ret) << "ReadXlPayloadLength should fail when stream cannot provide uint16_t";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortToDetermineLength)
                << "error should be BufferTooShortToDetermineLength";
        }
    }

    {
        // 11-bit SECDED max is 2047; decoded 2047 is valid. Test uncorrectable corruption instead.
        const uint16_t encoded = algorithms::Secded16Encode11(100);
        const uint16_t corrupted = encoded ^ 0x0300U;  // two bit flips -> uncorrectable
        uint8_t buffer[4] = {0};
        buffer[0] = static_cast<uint8_t>(corrupted >> 8U);
        buffer[1] = static_cast<uint8_t>(corrupted & 0xFFU);
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        bool dlc_corrected = false;
        size_t payload_len = 0;
        auto ret = ReadXlPayloadLength(stream, frame, dlc_corrected, payload_len);
        EXPECT_FALSE(ret) << "ReadXlPayloadLength should fail for uncorrectable SECDED";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::FormatDlcCorrupted) << "error should be FormatDlcCorrupted";
        }
    }
}

TEST(SpIOpen_FrameReader, ReadXlControl) {
    {
        const uint8_t payload_type = 0x12;
        const uint8_t vcid = 0x34;
        const uint32_t addr = 0x12345678U;
        uint8_t buffer[8] = {0};
        buffer[0] = payload_type;
        buffer[1] = vcid;
        buffer[2] = static_cast<uint8_t>(addr >> 24);
        buffer[3] = static_cast<uint8_t>(addr >> 16);
        buffer[4] = static_cast<uint8_t>(addr >> 8);
        buffer[5] = static_cast<uint8_t>(addr);
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        auto ret = ReadXlControl(stream, frame);
        ASSERT_TRUE(ret) << "ReadXlControl should succeed with 6 bytes in stream";
        EXPECT_EQ(frame.xl_control.payload_type, payload_type) << "payload_type should match stream";
        EXPECT_EQ(frame.xl_control.virtual_can_network_id, vcid) << "virtual_can_network_id should match stream";
        EXPECT_EQ(frame.xl_control.addressing_field, addr) << "addressing_field should match stream";
    }

    {
        uint8_t buffer[2] = {0};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        auto ret = ReadXlControl(stream, frame);
        EXPECT_FALSE(ret) << "ReadXlControl should fail when stream has fewer than 6 bytes";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForHeader)
                << "error should be BufferTooShortForHeader";
        }
    }
}
#endif

TEST(SpIOpen_FrameReader, ReadCanID) {
    {
        // Standard ID 0x7FF, big-endian: 0x07 0xFF
        const uint8_t buffer[] = {0x07U, 0xFFU};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        frame.can_flags.IDE = 0;
        auto ret = ReadCanID(stream, frame);
        ASSERT_TRUE(ret) << "ReadCanID should succeed for standard ID with 2 bytes in stream";
        EXPECT_EQ(frame.can_identifier, 0x7FFU) << "can_identifier should be 0x7FF for 0x07 0xFF big-endian";
    }

    {
        // Extended ID, big-endian 4 bytes
        const uint8_t buffer[] = {0x1FU, 0xFFU, 0xFFU, 0xFFU};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        frame.can_flags.IDE = 1;
        auto ret = ReadCanID(stream, frame);
        ASSERT_TRUE(ret) << "ReadCanID should succeed for extended ID with 4 bytes in stream";
        EXPECT_EQ(frame.can_identifier, 0x1FFFFFFFU) << "can_identifier should be 0x1FFFFFFF for extended";
    }

    {
        uint8_t buffer[1] = {0};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        frame.can_flags.IDE = 0;
        auto ret = ReadCanID(stream, frame);
        EXPECT_FALSE(ret) << "ReadCanID should fail when stream has only 1 byte for standard ID";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForHeader)
                << "error should be BufferTooShortForHeader";
        }
    }
}

TEST(SpIOpen_FrameReader, ReadTTL) {
    {
        const uint8_t buffer[] = {5};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        auto ret = ReadTTL(stream, frame);
        ASSERT_TRUE(ret) << "ReadTTL should succeed when stream has one byte";
        EXPECT_EQ(frame.time_to_live, 5U) << "time_to_live should be 5";
    }

    {
        const uint8_t buffer[1] = {0};
        etl::byte_stream_reader stream(static_cast<const void*>(buffer), size_t(0), etl::endian::big);
        Frame frame{};
        auto ret = ReadTTL(stream, frame);
        EXPECT_FALSE(ret) << "ReadTTL should fail when stream is empty";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForHeader)
                << "error should be BufferTooShortForHeader";
        }
    }
}

TEST(SpIOpen_FrameReader, ValidateCRC) {
    {
        // Build minimal frame + crc_region (e.g. 2 bytes), compute CRC16, put CRC in stream
        uint8_t crc_region_data[] = {0x01, 0x02};
        const uint16_t crc16 = algorithms::ComputeCrc16(etl::span<const uint8_t>(crc_region_data, 2));
        uint8_t stream_buf[4] = {0};
        stream_buf[0] = static_cast<uint8_t>(crc16 >> 8U);
        stream_buf[1] = static_cast<uint8_t>(crc16 & 0xFFU);
        etl::byte_stream_reader stream(stream_buf, sizeof(stream_buf), etl::endian::big);
        Frame frame{};
        frame.can_flags = {};
        uint8_t payload_storage[2] = {0x01, 0x02};
        frame.payload = etl::span<uint8_t>(payload_storage, 2);
        const etl::span<const uint8_t> crc_region(crc_region_data, 2);
        auto ret = ValidateCRC(stream, frame, crc_region);
        ASSERT_TRUE(ret) << "ValidateCRC should succeed when CRC16 matches";
    }

    {
        uint8_t crc_region_data[] = {0x01, 0x02};
        uint8_t stream_buf[] = {0x00, 0x00};  // wrong CRC
        etl::byte_stream_reader stream(stream_buf, sizeof(stream_buf), etl::endian::big);
        Frame frame{};
        frame.can_flags = {};
        uint8_t payload_storage[2] = {0x01, 0x02};
        frame.payload = etl::span<uint8_t>(payload_storage, 2);
        const etl::span<const uint8_t> crc_region(crc_region_data, 2);
        auto ret = ValidateCRC(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "ValidateCRC should fail when stream CRC does not match computed CRC";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::CrcMismatch) << "error should be CrcMismatch";
        }
    }

    {
        const uint8_t buffer[1] = {0};
        etl::byte_stream_reader stream(buffer, sizeof(buffer), etl::endian::big);
        Frame frame{};
        frame.can_flags = {};
        uint8_t payload_storage[2] = {0};
        frame.payload = etl::span<uint8_t>(payload_storage, 2);
        etl::span<const uint8_t> crc_region(payload_storage, 2);
        auto ret = ValidateCRC(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "ValidateCRC should fail when stream cannot provide 2-byte CRC16";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForPayload)
                << "error should be BufferTooShortForPayload";
        }
    }
}

TEST(SpIOpen_FrameReader, CopyFromBitSlippedBuffer) {
    {
        // bit_slip_count 0: plain copy
        uint8_t src_buf[] = {0xAA, 0xBB, 0xCC};
        uint8_t dest_buf[8] = {0};
        etl::byte_stream_reader src(src_buf, sizeof(src_buf), etl::endian::big);
        etl::byte_stream_writer dest(etl::span<uint8_t>(dest_buf, sizeof(dest_buf)), etl::endian::big);
        auto ret = CopyFromBitSlippedBuffer(src, dest, 3, 0);
        ASSERT_TRUE(ret) << "CopyFromBitSlippedBuffer should succeed for bit_slip_count 0 with enough data";
        EXPECT_EQ(dest_buf[0], 0xAA) << "first copied byte should be 0xAA";
        EXPECT_EQ(dest_buf[1], 0xBB) << "second copied byte should be 0xBB";
        EXPECT_EQ(dest_buf[2], 0xCC) << "third copied byte should be 0xCC";
    }

    {
        // bit_slip_count 1: complement
        uint8_t src_buf[] = {PREAMBLE_BYTE_COMPLEMENT, PREAMBLE_BYTE_COMPLEMENT, PREAMBLE_BYTE_COMPLEMENT};
        uint8_t dest_buf[8] = {0};
        etl::byte_stream_reader src(src_buf, sizeof(src_buf), etl::endian::big);
        etl::byte_stream_writer dest(etl::span<uint8_t>(dest_buf, sizeof(dest_buf)), etl::endian::big);
        auto ret = CopyFromBitSlippedBuffer(src, dest, 2, 1);
        ASSERT_TRUE(ret) << "CopyFromBitSlippedBuffer should succeed for bit_slip_count 1 with enough data";
        EXPECT_EQ(dest_buf[0], PREAMBLE_BYTE) << "first copied byte should be 0xAA";
        EXPECT_EQ(dest_buf[1], PREAMBLE_BYTE) << "second copied byte should be 0xAA";
    }

    {
        // bit_slip_count > 7: invalid
        uint8_t src_buf[] = {0xAA, 0xBB};
        uint8_t dest_buf[8] = {0};
        etl::byte_stream_reader src(src_buf, sizeof(src_buf), etl::endian::big);
        etl::byte_stream_writer dest(etl::span<uint8_t>(dest_buf, sizeof(dest_buf)), etl::endian::big);
        auto ret = CopyFromBitSlippedBuffer(src, dest, 1, 8);
        EXPECT_FALSE(ret) << "CopyFromBitSlippedBuffer should fail when bit_slip_count > 7";
    }

    {
        // dest too small
        uint8_t src_buf[] = {0xAA, 0xBB};
        uint8_t dest_buf[1] = {0};
        etl::byte_stream_reader src(src_buf, sizeof(src_buf), etl::endian::big);
        etl::byte_stream_writer dest(etl::span<uint8_t>(dest_buf, sizeof(dest_buf)), etl::endian::big);
        auto ret = CopyFromBitSlippedBuffer(src, dest, 2, 0);
        EXPECT_FALSE(ret) << "CopyFromBitSlippedBuffer should fail when dest has fewer bytes than bytes_to_copy";
    }

    {
        // source too small for bytes_to_copy
        uint8_t src_buf[] = {0xAA};
        uint8_t dest_buf[8] = {0};
        etl::byte_stream_reader src(src_buf, sizeof(src_buf), etl::endian::big);
        etl::byte_stream_writer dest(etl::span<uint8_t>(dest_buf, sizeof(dest_buf)), etl::endian::big);
        auto ret = CopyFromBitSlippedBuffer(src, dest, 2, 0);
        EXPECT_FALSE(ret) << "CopyFromBitSlippedBuffer should fail when source has fewer bytes than bytes_to_copy";
    }
}

TEST(SpIOpen_FrameReader, FindNextPreambleByte) {
    {
        uint8_t buffer[] = {0x00, PREAMBLE_BYTE, 0x00};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = FindNextPreambleByte(buf_span, 0, false);
        ASSERT_TRUE(ret) << "FindNextPreambleByte should find PREAMBLE_BYTE at index 1 when bit_slips_allowed false";
        EXPECT_EQ(*ret, 1U) << "returned index should be 1";
    }

    {
        uint8_t buffer[] = {0x00, PREAMBLE_BYTE_COMPLEMENT, 0x00};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = FindNextPreambleByte(buf_span, 0, true);
        ASSERT_TRUE(ret) << "FindNextPreambleByte should find complement at index 1 when bit_slips_allowed true";
        EXPECT_EQ(*ret, 1U) << "returned index should be 1";
    }

    {
        uint8_t buffer[] = {0x00, 0x00};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = FindNextPreambleByte(buf_span, 0, false);
        EXPECT_FALSE(ret) << "FindNextPreambleByte should fail when no preamble byte in buffer";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::NoPreamble) << "error should be NoPreamble";
        }
    }

    {
        uint8_t buffer[] = {0x00};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = FindNextPreambleByte(buf_span, 1, false);
        EXPECT_FALSE(ret) << "FindNextPreambleByte should fail when offset >= buffer.size()";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForPreamble)
                << "error should be BufferTooShortForPreamble";
        }
    }
}

TEST(SpIOpen_FrameReader, CountBitOffsetIntoPreviousByte) {
    {
        // Aligned 2-byte preamble at start
        uint8_t buffer[] = {PREAMBLE_BYTE, PREAMBLE_BYTE};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = CountBitOffsetIntoPreviousByte(buf_span, 0);
        ASSERT_TRUE(ret) << "CountBitOffsetIntoPreviousByte should succeed for aligned 2-byte preamble at start";
        EXPECT_EQ(*ret, 0U) << "bit offset should be 0 when preamble is aligned";
    }
    {
        // Even bit slip: pattern with preamble at index 1; bit offset from previous byte
        uint8_t buffer[] = {PREAMBLE_BYTE >> 2, PREAMBLE_BYTE, PREAMBLE_BYTE};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = CountBitOffsetIntoPreviousByte(buf_span, 1);
        ASSERT_TRUE(ret) << "CountBitOffsetIntoPreviousByte should succeed for even bit slip pattern";
        EXPECT_EQ(*ret, 6U) << "bit offset value from implementation for this pattern";
    }

    {
        // Odd bit slip (complement preamble at index 1)
        uint8_t buffer[] = {PREAMBLE_BYTE_COMPLEMENT, PREAMBLE_BYTE_COMPLEMENT, PREAMBLE_BYTE_COMPLEMENT};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = CountBitOffsetIntoPreviousByte(buf_span, 1);
        ASSERT_TRUE(ret) << "CountBitOffsetIntoPreviousByte should succeed for odd bit slip (complement)";
        EXPECT_EQ(*ret, 7U) << "bit offset value from implementation for complement pattern";
    }

    {
        // Buffer too short: preamble_index + 1 >= size
        uint8_t buffer[] = {PREAMBLE_BYTE};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = CountBitOffsetIntoPreviousByte(buf_span, 0);
        EXPECT_FALSE(ret) << "CountBitOffsetIntoPreviousByte should fail when buffer has no byte after preamble_index";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::BufferTooShortForPreamble)
                << "error should be BufferTooShortForPreamble";
        }
    }

    {
        // First byte not matching second for 2-byte preamble at index 0
        uint8_t buffer[] = {PREAMBLE_BYTE, 0x00};
        etl::span<uint8_t> buf_span(buffer, sizeof(buffer));
        auto ret = CountBitOffsetIntoPreviousByte(buf_span, 0);
        EXPECT_FALSE(ret) << "CountBitOffsetIntoPreviousByte should fail when second byte is not preamble/complement";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameParseError::NoPreamble) << "error should be NoPreamble";
        }
    }
}
