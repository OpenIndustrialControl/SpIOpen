#include <etl/byte_stream.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame.h"
#include "spiopen_frame_format.h"
#include "spiopen_frame_writer.h"

using namespace spiopen;
using namespace spiopen::format;
using namespace spiopen::frame_writer;
using namespace spiopen::frame_writer::impl;

TEST(SpIOpen_FrameWriter, ValidateFrame) {
    // --- Success: CC frame, empty payload, buffer large enough ---
    {
        Frame frame{};
        frame.can_flags = {};
        frame.payload = etl::span<uint8_t>();
        uint8_t buffer[format::MAX_CAN_CC_FRAME_SIZE] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        ASSERT_TRUE(ret) << "ValidateFrame should succeed for valid CC frame with empty payload";
    }

    // --- Success: CC frame, max CC payload (8 bytes), buffer large enough ---
    {
        uint8_t payload_storage[MAX_CC_PAYLOAD_SIZE] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.payload = etl::span<uint8_t>(payload_storage, MAX_CC_PAYLOAD_SIZE);
        uint8_t buffer[format::MAX_CAN_CC_FRAME_SIZE] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        ASSERT_TRUE(ret) << "ValidateFrame should succeed for CC frame with 8-byte payload";
    }

    // --- InvalidPayloadLength: CC frame with payload size > 8 ---
    {
        uint8_t payload_storage[9] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.payload = etl::span<uint8_t>(payload_storage, 9);
        uint8_t buffer[format::MAX_CAN_CC_FRAME_SIZE + 10] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        EXPECT_FALSE(ret) << "ValidateFrame should fail for CC payload > 8 bytes";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameWriteError::InvalidPayloadLength);
        }
    }

    // --- BufferTooShort: valid frame but stream has insufficient space ---
    {
        uint8_t payload_storage[2] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.payload = etl::span<uint8_t>(payload_storage, 2);
        size_t frame_len = 0;
        ASSERT_TRUE(frame.TryGetFrameLength(frame_len));
        // CC 2-byte payload frame is e.g. preamble 2 + header 2 + can_id 2 + payload 2 + crc 2 = 10 bytes
        const size_t too_small = frame_len > 1 ? frame_len - 1 : 1;
        uint8_t buffer[32] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, too_small), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        EXPECT_FALSE(ret) << "ValidateFrame should fail when stream has fewer bytes than frame length";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        }
    }

    // --- CanXlNotSupported: XL not enabled but frame has XLF set (when FD is enabled, TryGetPayloadSectionLength can
    // succeed for XL) ---
#ifndef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    {
        uint8_t payload_storage[9] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 9);
        uint8_t buffer[256] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        EXPECT_FALSE(ret)
            << "ValidateFrame should fail with CanXlNotSupported when XL not enabled and frame has XLF set";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameWriteError::CanXlNotSupported);
        }
    }
#endif

    // --- InvalidPayloadLength: FD frame with payload > 64 bytes ---
    {
        uint8_t payload_storage[65] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.can_flags.FDF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 65);
        uint8_t buffer[512] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        EXPECT_FALSE(ret) << "ValidateFrame should fail for FD payload > 64 bytes";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameWriteError::InvalidPayloadLength);
        }
    }

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    // --- InvalidPayloadLength: XL frame with payload > 2048 bytes ---
    {
        uint8_t payload_storage[2049] = {0};
        Frame frame{};
        frame.can_flags = {};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 2049);
        uint8_t buffer[4096] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = ValidateFrame(stream, frame);
        EXPECT_FALSE(ret) << "ValidateFrame should fail for XL payload > 2048 bytes";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameWriteError::InvalidPayloadLength);
        }
    }
#endif
}

static etl::span<const uint8_t> MakeCrcRegionFromStream(etl::byte_stream_writer& stream, size_t skip_bytes = 0) {
    etl::span<char> used = stream.used_data();
    const size_t n = used.size() > skip_bytes ? used.size() - skip_bytes : 0U;
    return etl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(used.data()) + skip_bytes, n);
}

TEST(SpIOpen_FrameWriter, WritePreamble) {
    constexpr uint8_t kExpectedPreambleByte = PREAMBLE_BYTE;

    {
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WritePreamble(stream);
        ASSERT_TRUE(ret) << "WritePreamble should succeed with sufficient buffer";
        EXPECT_EQ(stream.size_bytes(), PREAMBLE_SIZE);
        EXPECT_EQ(buffer[0], kExpectedPreambleByte) << "first preamble byte should be PREAMBLE_BYTE";
        EXPECT_EQ(buffer[1], kExpectedPreambleByte) << "second preamble byte should be PREAMBLE_BYTE";
    }

    {
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, sizeof(buffer) - 3), etl::endian::big);
        auto ret = WritePreamble(stream);
        ASSERT_TRUE(ret) << "WritePreamble should succeed at offset";
        EXPECT_EQ(stream.size_bytes(), PREAMBLE_SIZE);
        EXPECT_EQ(buffer[3], kExpectedPreambleByte);
        EXPECT_EQ(buffer[4], kExpectedPreambleByte);
    }

    {
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        auto ret = WritePreamble(stream);
        EXPECT_FALSE(ret) << "WritePreamble should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 1U), etl::endian::big);
        auto ret = WritePreamble(stream);
        EXPECT_FALSE(ret) << "WritePreamble should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, 1U), etl::endian::big);
        auto ret = WritePreamble(stream);
        EXPECT_FALSE(ret) << "WritePreamble should fail when cursor near end";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 2, 0U), etl::endian::big);
        auto ret = WritePreamble(stream);
        EXPECT_FALSE(ret) << "WritePreamble should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }
}

TEST(SpIOpen_FrameWriter, WriteFormatHeader) {
    uint8_t payload_storage[2] = {0};
    Frame frame{};
    frame.can_flags = {};
    frame.can_flags.IDE = 0;
    frame.can_flags.FDF = 0;
    frame.can_flags.XLF = 0;
    frame.can_flags.TTL = 0;
    frame.can_flags.WA = 0;
    frame.payload = etl::span<uint8_t>(payload_storage, 2);
    uint8_t buffer[8] = {0};

    {
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WriteFormatHeader(stream, frame);
        ASSERT_TRUE(ret) << "WriteFormatHeader should succeed with sufficient buffer";
        EXPECT_EQ(stream.size_bytes(), FORMAT_HEADER_SIZE);
    }

    {
        uint8_t small_buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(small_buffer, 0U), etl::endian::big);
        auto ret = WriteFormatHeader(stream, frame);
        EXPECT_FALSE(ret) << "WriteFormatHeader should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t short_buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(short_buffer + 3, 1U), etl::endian::big);
        auto ret = WriteFormatHeader(stream, frame);
        EXPECT_FALSE(ret) << "WriteFormatHeader should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t short_buffer[5] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(short_buffer + 4, 1U), etl::endian::big);
        auto ret = WriteFormatHeader(stream, frame);
        EXPECT_FALSE(ret) << "WriteFormatHeader should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }
}

TEST(SpIOpen_FrameWriter, WriteCanIdentifier) {
    constexpr uint32_t kStandardId = 0x7FFU;
    constexpr uint8_t kStandardIdHigh = 0x07U;
    constexpr uint8_t kStandardIdLow = 0xFFU;
    constexpr uint32_t kExtendedId = 0x1FFFFFFFU;
    constexpr uint8_t kExtendedIdHigh = 0x1FU;

    {
        Frame frame{};
        frame.can_flags = {};
        frame.can_flags.IDE = 0;
        frame.can_identifier = kStandardId;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        ASSERT_TRUE(ret) << "WriteCanIdentifier should succeed for standard ID";
        EXPECT_EQ(stream.size_bytes(), CAN_IDENTIFIER_SIZE);
        EXPECT_EQ(buffer[0], kStandardIdHigh) << "first CAN identifier byte should be high byte of standard identifier";
        EXPECT_EQ(buffer[1], kStandardIdLow);
    }

    {
        Frame frame{};
        frame.can_flags = {};
        frame.can_flags.IDE = 1;
        frame.can_identifier = kExtendedId;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        ASSERT_TRUE(ret) << "WriteCanIdentifier should succeed for extended ID";
        EXPECT_EQ(stream.size_bytes(), CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE);
        EXPECT_EQ(buffer[0], kExtendedIdHigh) << "first CAN identifier byte should be high byte of extended identifier";
        EXPECT_EQ(buffer[1], 0xFFU);
        EXPECT_EQ(buffer[2], 0xFFU);
        EXPECT_EQ(buffer[3], 0xFFU);
    }

    {
        Frame frame{};
        frame.can_flags.IDE = 0;
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        EXPECT_FALSE(ret) << "WriteCanIdentifier should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        Frame frame{};
        frame.can_flags.IDE = 0;
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 1U), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        EXPECT_FALSE(ret) << "WriteCanIdentifier should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        Frame frame{};
        frame.can_flags.IDE = 1;
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, 1U), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        EXPECT_FALSE(ret) << "WriteCanIdentifier should fail for extended when no room";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        Frame frame{};
        frame.can_flags.IDE = 0;
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, 1U), etl::endian::big);
        auto ret = WriteCanIdentifier(stream, frame);
        EXPECT_FALSE(ret) << "WriteCanIdentifier should fail when cursor near end";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }
}

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
TEST(SpIOpen_FrameWriter, WriteXlDataAndControl) {
    constexpr uint8_t kPayloadType = 0x12;
    constexpr uint8_t kVirtualCanNetworkId = 0x34;
    constexpr uint32_t kAddressingField = 0x12345678U;

    {
        uint8_t payload_storage[10] = {0};
        Frame frame{};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 10);
        frame.xl_control.payload_type = kPayloadType;
        frame.xl_control.virtual_can_network_id = kVirtualCanNetworkId;
        frame.xl_control.addressing_field = kAddressingField;
        uint8_t buffer[16] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WriteXlDataAndControl(stream, frame);
        ASSERT_TRUE(ret) << "WriteXlDataAndControl should succeed with sufficient buffer";
        EXPECT_EQ(stream.size_bytes(), 8U);
        EXPECT_EQ(buffer[2], kPayloadType);
        EXPECT_EQ(buffer[3], kVirtualCanNetworkId);
        EXPECT_EQ(buffer[4], 0x12);
        EXPECT_EQ(buffer[5], 0x34);
        EXPECT_EQ(buffer[6], 0x56);
        EXPECT_EQ(buffer[7], 0x78);
    }

    {
        Frame frame{};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>();
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        auto ret = WriteXlDataAndControl(stream, frame);
        EXPECT_FALSE(ret) << "WriteXlDataAndControl should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t payload_storage[1] = {0};
        Frame frame{};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 1);
        uint8_t buffer[10] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 5, 5U), etl::endian::big);
        auto ret = WriteXlDataAndControl(stream, frame);
        EXPECT_FALSE(ret) << "WriteXlDataAndControl should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }

    {
        uint8_t payload_storage[1] = {0};
        Frame frame{};
        frame.can_flags.XLF = 1;
        frame.payload = etl::span<uint8_t>(payload_storage, 1);
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 8, 0U), etl::endian::big);
        auto ret = WriteXlDataAndControl(stream, frame);
        EXPECT_FALSE(ret) << "WriteXlDataAndControl should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }
}
#endif

TEST(SpIOpen_FrameWriter, WriteTimeToLive) {
    {
        Frame frame{};
        frame.can_flags.TTL = 1;
        frame.time_to_live = 5;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WriteTimeToLive(stream, frame);
        ASSERT_TRUE(ret) << "WriteTimeToLive should succeed when TTL set";
        EXPECT_EQ(stream.size_bytes(), TIME_TO_LIVE_SIZE);
        EXPECT_EQ(buffer[0], 5U) << "TTL byte should match frame.time_to_live";
    }

    {
        Frame frame{};
        frame.can_flags.TTL = 0;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, sizeof(buffer) - 3), etl::endian::big);
        auto ret = WriteTimeToLive(stream, frame);
        ASSERT_TRUE(ret) << "WriteTimeToLive should succeed when TTL not set";
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        Frame frame{};
        frame.can_flags.TTL = 1;
        frame.time_to_live = 1;
        uint8_t buffer[1] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        auto ret = WriteTimeToLive(stream, frame);
        EXPECT_FALSE(ret) << "WriteTimeToLive should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        Frame frame{};
        frame.can_flags.TTL = 1;
        frame.time_to_live = 1;
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 4, 0U), etl::endian::big);
        auto ret = WriteTimeToLive(stream, frame);
        EXPECT_FALSE(ret) << "WriteTimeToLive should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }
}

TEST(SpIOpen_FrameWriter, WritePayload) {
    {
        Frame frame{};
        frame.payload = etl::span<uint8_t>();
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        ASSERT_TRUE(ret) << "WritePayload should succeed with zero length";
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t payload[] = {0x01, 0x02, 0x03};
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload, 3);
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        ASSERT_TRUE(ret) << "WritePayload should succeed with payload data";
        EXPECT_EQ(stream.size_bytes(), 3U);
        EXPECT_EQ(buffer[0], 0x01);
        EXPECT_EQ(buffer[1], 0x02);
        EXPECT_EQ(buffer[2], 0x03);
    }

    {
        uint8_t payload[] = {0xAB, 0, 0, 0};
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload, 4);
        uint8_t buffer[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        ASSERT_TRUE(ret) << "WritePayload should succeed with payload and padding";
        EXPECT_EQ(stream.size_bytes(), 4U);
        EXPECT_EQ(buffer[0], 0xAB);
        EXPECT_EQ(buffer[1], 0);
        EXPECT_EQ(buffer[2], 0);
        EXPECT_EQ(buffer[3], 0);
    }

    {
        uint8_t payload[] = {0x01};
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload, 1);
        uint8_t buffer[1] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        EXPECT_FALSE(ret) << "WritePayload should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t payload[] = {0x01, 0x02, 0x03};
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload, 3);
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 6, 2U), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        EXPECT_FALSE(ret) << "WritePayload should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }

    {
        uint8_t payload[] = {0x01, 0x02};
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload, 2);
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 3, 1U), etl::endian::big);
        auto ret = WritePayload(stream, frame);
        EXPECT_FALSE(ret) << "WritePayload should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
        EXPECT_EQ(stream.size_bytes(), 0U);
    }
}

TEST(SpIOpen_FrameWriter, WriteCrc) {
    {
        uint8_t payload_storage[3] = {0};
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        ASSERT_TRUE(WritePreamble(stream));
        stream.write_unchecked(static_cast<uint8_t>(0x01));
        stream.write_unchecked(static_cast<uint8_t>(0x02));
        stream.write_unchecked(static_cast<uint8_t>(0x03));
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload_storage, 3);
        auto crc_region = MakeCrcRegionFromStream(stream, PREAMBLE_SIZE);
        auto ret = WriteCrc(stream, frame, crc_region);
        ASSERT_TRUE(ret) << "WriteCrc should succeed for CRC16";
        EXPECT_EQ(stream.size_bytes(), PREAMBLE_SIZE + 3U + SHORT_CRC_SIZE);
    }

    {
        uint8_t payload_storage[9] = {0};
        uint8_t buffer[16] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        ASSERT_TRUE(WritePreamble(stream));
        for (uint8_t i = 1; i <= 9; ++i) {
            stream.write_unchecked(i);
        }
        Frame frame{};
        frame.can_flags.FDF = 1;  // FD frame so TryGetPayloadSectionLength accepts 9-byte payload
        frame.payload = etl::span<uint8_t>(payload_storage, 9);
        auto crc_region = MakeCrcRegionFromStream(stream, PREAMBLE_SIZE);
        auto ret = WriteCrc(stream, frame, crc_region);
        ASSERT_TRUE(ret) << "WriteCrc should succeed for CRC32";
        EXPECT_EQ(stream.size_bytes(), PREAMBLE_SIZE + 9U + LONG_CRC_SIZE);
    }

    {
        uint8_t payload_storage[1] = {0};
        uint8_t buffer[2] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 0U), etl::endian::big);
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload_storage, 1);
        auto crc_region = MakeCrcRegionFromStream(stream);
        auto ret = WriteCrc(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "WriteCrc should fail with zero-length buffer";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }

    {
        // Room for preamble + 1 byte (3 bytes used), but only 1 free; CRC16 needs 2 bytes so WriteCrc fails.
        uint8_t payload_storage[1] = {0};
        uint8_t buffer[5] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 4U), etl::endian::big);
        ASSERT_TRUE(WritePreamble(stream));
        stream.write_unchecked(static_cast<uint8_t>(1));
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload_storage, 1);
        auto crc_region = MakeCrcRegionFromStream(stream, PREAMBLE_SIZE);
        auto ret = WriteCrc(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "WriteCrc should fail when buffer too short";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }

    {
        uint8_t payload_storage[9] = {0};
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 5, 3U), etl::endian::big);
        ASSERT_TRUE(WritePreamble(stream));
        stream.write_unchecked(static_cast<uint8_t>(1));
        Frame frame{};
        frame.can_flags.FDF = 1;  // FD frame so TryGetPayloadSectionLength accepts 9-byte payload
        frame.payload = etl::span<uint8_t>(payload_storage, 9);
        auto crc_region = MakeCrcRegionFromStream(stream, PREAMBLE_SIZE);
        auto ret = WriteCrc(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "WriteCrc should fail when no room for CRC32";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }

    {
        uint8_t payload_storage[1] = {0};
        uint8_t buffer[4] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 2, 2U), etl::endian::big);
        ASSERT_TRUE(WritePreamble(stream));
        Frame frame{};
        frame.payload = etl::span<uint8_t>(payload_storage, 1);
        auto crc_region = MakeCrcRegionFromStream(stream, PREAMBLE_SIZE);
        auto ret = WriteCrc(stream, frame, crc_region);
        EXPECT_FALSE(ret) << "WriteCrc should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }
}

TEST(SpIOpen_FrameWriter, WriteFramePadding) {
    {
        Frame frame{};
        frame.can_flags.WA = 1;
        uint8_t buffer[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, sizeof(buffer)), etl::endian::big);
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        auto ret = WriteFramePadding(stream, frame);
        ASSERT_TRUE(ret) << "WriteFramePadding should succeed when word align and odd length";
        EXPECT_EQ(stream.size_bytes(), 3U + MAX_PADDING_SIZE);
        EXPECT_EQ(buffer[3], 0);
    }

    {
        Frame frame{};
        frame.can_flags.WA = 0;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 5, 3U), etl::endian::big);
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        auto ret = WriteFramePadding(stream, frame);
        ASSERT_TRUE(ret) << "WriteFramePadding should succeed when word align disabled";
        EXPECT_EQ(stream.size_bytes(), 3U);
    }

    {
        Frame frame{};
        frame.can_flags.WA = 1;
        uint8_t buffer[8] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 2, 6U), etl::endian::big);
        stream.write_unchecked(static_cast<uint16_t>(0));
        auto ret = WriteFramePadding(stream, frame);
        ASSERT_TRUE(ret) << "WriteFramePadding should succeed when length already even";
        EXPECT_EQ(stream.size_bytes(), 2U);
    }

    {
        // Stream with 3 bytes written (odd length after preamble) and 0 free: must write 1 padding byte but cannot.
        Frame frame{};
        frame.can_flags.WA = 1;
        uint8_t buffer[3] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer, 3U), etl::endian::big);
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        stream.write_unchecked(static_cast<uint8_t>(0));
        auto ret = WriteFramePadding(stream, frame);
        EXPECT_FALSE(ret) << "WriteFramePadding should fail when no room for padding byte";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }

    {
        // Stream with 1 byte written and 0 free (cursor at end): current_length underflows; implementation may or may
        // not try to write.
        Frame frame{};
        frame.can_flags.WA = 1;
        uint8_t buffer[5] = {0};
        etl::byte_stream_writer stream(etl::span<uint8_t>(buffer + 4, 1U), etl::endian::big);
        stream.write_unchecked(static_cast<uint8_t>(0));
        auto ret = WriteFramePadding(stream, frame);
        EXPECT_FALSE(ret) << "WriteFramePadding should fail when no room at cursor";
        EXPECT_EQ(ret.error(), FrameWriteError::BufferTooShort);
    }
}
