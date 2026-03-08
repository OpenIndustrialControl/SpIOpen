#include <etl/byte_stream.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "spiopen_frame.h"
#include "spiopen_frame_buffer.h"
#include "spiopen_frame_format.h"
#include "spiopen_frame_reader.h"
#include "spiopen_frame_writer.h"

using namespace spiopen;
using namespace spiopen::format;
using namespace spiopen::frame_reader::impl;

TEST(SpIOpen_FrameBuffer, ConstructorAndFields) {
    etl::span<uint8_t> empty_span;
    FrameBuffer empty_buffer = FrameBuffer(empty_span);
    EXPECT_EQ(empty_buffer.GetBuffer().data(), nullptr) << "Buffer data";
    EXPECT_EQ(empty_buffer.GetBuffer().size(), 0U) << "Buffer length";

    uint8_t* buffer_data = new uint8_t[8]{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    etl::span<uint8_t> buffer_span(buffer_data, 8U);
    FrameBuffer test_buffer = FrameBuffer(buffer_span);
    EXPECT_EQ(test_buffer.GetBuffer().data(), buffer_data) << "Buffer data";
    EXPECT_EQ(test_buffer.GetBuffer().size(), 8U) << "Buffer length";
}

TEST(SpIOpen_FrameBuffer, UpdateInternalBuffer) {
    // --- Success: minimal CC frame with 2-byte payload, buffer large enough ---
    {
        constexpr size_t kBufferSize = 64;
        uint8_t buffer[kBufferSize] = {0};
        etl::span<uint8_t> buffer_span(buffer, kBufferSize);
        FrameBuffer fb(buffer_span);

        uint8_t payload_data[] = {0xAB, 0xCD};
        Frame& frame = fb.GetFrame();
        frame.can_flags = {};
        frame.can_identifier = 0x123U;
        frame.payload = etl::span<uint8_t>(payload_data, 2);

        auto ret = fb.UpdateInternalBuffer();
        ASSERT_TRUE(ret) << "UpdateInternalBuffer should succeed for valid CC frame with sufficient buffer";

        EXPECT_EQ(buffer[0], PREAMBLE_BYTE) << "first byte of internal buffer should be preamble";
        EXPECT_EQ(buffer[1], PREAMBLE_BYTE) << "second byte of internal buffer should be preamble";
        // Payload appears after preamble + format header + CAN ID (2+2+2 = 6 bytes)
        size_t payload_offset = PREAMBLE_SIZE + FORMAT_HEADER_SIZE + CAN_IDENTIFIER_SIZE;
        EXPECT_EQ(buffer[payload_offset], 0xAB) << "first payload byte in buffer should match written frame";
        EXPECT_EQ(buffer[payload_offset + 1], 0xCD) << "second payload byte in buffer should match written frame";
    }

    // --- Success: empty payload (zero-length frame) ---
    {
        constexpr size_t kBufferSize = 32;
        uint8_t buffer[kBufferSize] = {0};
        etl::span<uint8_t> buffer_span(buffer, kBufferSize);
        FrameBuffer fb(buffer_span);

        Frame& frame = fb.GetFrame();
        frame.can_flags = {};
        frame.can_identifier = 0;
        frame.payload = etl::span<uint8_t>();

        auto ret = fb.UpdateInternalBuffer();
        ASSERT_TRUE(ret) << "UpdateInternalBuffer should succeed for frame with empty payload";
        EXPECT_EQ(buffer[0], PREAMBLE_BYTE) << "internal buffer should start with preamble";
    }

    // --- Failure: buffer too short for frame ---
    {
        constexpr size_t kBufferSize = 32;
        uint8_t buffer[kBufferSize] = {0};
        etl::span<uint8_t> buffer_span(buffer, 8);  // only 8 bytes available
        FrameBuffer fb(buffer_span);

        uint8_t payload_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        Frame& frame = fb.GetFrame();
        frame.can_flags = {};
        frame.can_identifier = 0x100U;
        frame.payload = etl::span<uint8_t>(payload_data, 6);

        auto ret = fb.UpdateInternalBuffer();
        EXPECT_FALSE(ret) << "UpdateInternalBuffer should fail when internal buffer is too short for frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_writer::FrameWriteError::BufferTooShort)
                << "error should be BufferTooShort when buffer too small";
        }
    }
}

TEST(SpIOpen_FrameBuffer, UpdateInternalFrame) {
    // --- Success: buffer contains valid CC frame; read and check frame fields and payload ---
    {
        constexpr size_t kBufferSize = 64;
        uint8_t buffer[kBufferSize] = {0};
        etl::span<uint8_t> buffer_span(buffer, kBufferSize);

        // Build a valid frame and write it into the buffer using the writer
        uint8_t payload_data[] = {0x11, 0x22};
        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0x300U;
        frame_to_write.payload = etl::span<uint8_t>(payload_data, 2);

        etl::byte_stream_writer writer(buffer_span, etl::endian::big);
        auto write_ret = frame_writer::WriteFrame(writer, frame_to_write);
        ASSERT_TRUE(write_ret) << "WriteFrame must succeed to populate buffer for UpdateInternalFrame test";

        FrameBuffer fb(buffer_span);
        auto ret = fb.UpdateInternalFrame();
        ASSERT_TRUE(ret)
            << "UpdateInternalFrame should succeed when buffer holds valid serialized frame (parse error code: "
            << static_cast<std::underlying_type_t<frame_reader::FrameParseError>>(ret.error()) << ")";

        Frame& read_frame = fb.GetFrame();
        EXPECT_EQ(read_frame.can_identifier, 0x300U) << "read frame can_identifier should match written value";
        EXPECT_FALSE(read_frame.payload.empty()) << "read frame payload span should be non-empty for 2-byte payload";
        EXPECT_EQ(read_frame.payload.size(), 2U) << "read frame payload size should be 2";
        EXPECT_EQ(read_frame.payload.data(), buffer + (PREAMBLE_SIZE + FORMAT_HEADER_SIZE + CAN_IDENTIFIER_SIZE))
            << "payload should point into internal buffer after preamble and header";
        EXPECT_EQ(read_frame.payload[0], 0x11) << "first payload byte in read frame should match written data";
        EXPECT_EQ(read_frame.payload[1], 0x22) << "second payload byte in read frame should match written data";
    }

    // --- Success: frame with zero-length payload ---
    {
        constexpr size_t kBufferSize = 32;
        uint8_t buffer[kBufferSize] = {0};
        etl::span<uint8_t> buffer_span(buffer, kBufferSize);

        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0;
        frame_to_write.payload = etl::span<uint8_t>();

        etl::byte_stream_writer writer(buffer_span, etl::endian::big);
        auto write_ret = frame_writer::WriteFrame(writer, frame_to_write);
        ASSERT_TRUE(write_ret) << "WriteFrame must succeed for empty-payload frame";

        FrameBuffer fb(buffer_span);
        auto ret = fb.UpdateInternalFrame();
        ASSERT_TRUE(ret) << "UpdateInternalFrame should succeed for buffer containing empty-payload frame";
        EXPECT_TRUE(fb.GetFrame().payload.empty()) << "read frame payload should be empty";
    }

    // --- Failure: buffer too short to parse (truncated) ---
    {
        uint8_t buffer[] = {PREAMBLE_BYTE, PREAMBLE_BYTE};  // only preamble, no format header
        etl::span<uint8_t> buffer_span(buffer, sizeof(buffer));
        FrameBuffer fb(buffer_span);

        auto ret = fb.UpdateInternalFrame();
        EXPECT_FALSE(ret) << "UpdateInternalFrame should fail when buffer is truncated before full header";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortToDetermineLength)
                << "error should be BufferTooShortToDetermineLength for truncated buffer";
        }
    }

    // --- Failure: no valid preamble at start ---
    {
        constexpr size_t kBufferSize = 16;
        uint8_t buffer[kBufferSize] = {0};
        buffer[0] = 0x00;
        buffer[1] = 0x00;
        etl::span<uint8_t> buffer_span(buffer, kBufferSize);
        FrameBuffer fb(buffer_span);

        auto ret = fb.UpdateInternalFrame();
        EXPECT_FALSE(ret) << "UpdateInternalFrame should fail when buffer does not start with valid preamble";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::NoPreamble)
                << "error should be NoPreamble when first bytes are not 0xAA 0xAA";
        }
    }
}

TEST(SpIOpen_FrameBuffer, CopyToInternalBuffer) {
    // --- Success: input stream with valid frame, bit_slip_count 0 ---
    {
        constexpr size_t kInputSize = 64;
        constexpr size_t kBufferSize = 64;
        uint8_t input_buf[kInputSize] = {0};
        uint8_t internal_buf[kBufferSize] = {0};

        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0x50U;
        uint8_t payload_data[] = {0xDD, 0xEE};
        frame_to_write.payload = etl::span<uint8_t>(payload_data, 2);

        etl::byte_stream_writer writer(etl::span<uint8_t>(input_buf, kInputSize), etl::endian::big);
        auto write_ret = frame_writer::WriteFrame(writer, frame_to_write);
        ASSERT_TRUE(write_ret) << "WriteFrame must succeed to build input stream for CopyToInternalBuffer";

        etl::byte_stream_reader input_stream(input_buf, kInputSize, etl::endian::big);
        FrameBuffer fb(etl::span<uint8_t>(internal_buf, kBufferSize));

        auto ret = fb.CopyToInternalBuffer(input_stream, 0);
        ASSERT_TRUE(ret) << "CopyToInternalBuffer should succeed when input stream has valid frame (error: "
                         << static_cast<int>(ret.error()) << ")";

        Frame& read_frame = fb.GetFrame();
        EXPECT_EQ(read_frame.can_identifier, 0x50U) << "read frame can_identifier should match frame in input stream";
        EXPECT_EQ(read_frame.payload.size(), 2U) << "read frame payload size should be 2";
        EXPECT_EQ(read_frame.payload[0], 0xDD) << "payload byte 0 should match after load and read";
        EXPECT_EQ(read_frame.payload[1], 0xEE) << "payload byte 1 should match after load and read";
        EXPECT_TRUE(read_frame.payload.data() >= internal_buf && read_frame.payload.data() < internal_buf + kBufferSize)
            << "payload should point into internal buffer after CopyToInternalBuffer";
    }

    // --- Success: 1-bit slipped data in input_buf, CopyFromBitSlippedBuffer moves to slipped_buf, then parse ---
    {
        constexpr size_t kInputSize = 64;
        constexpr size_t kBufferSize = 64;
        uint8_t input_buf[kInputSize] = {0};
        uint8_t slipped_buf[kInputSize] = {0};
        uint8_t internal_buf[kBufferSize] = {0};

        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0x50U;
        uint8_t payload_data[] = {0xDD, 0xEE};
        frame_to_write.payload = etl::span<uint8_t>(payload_data, 2);

        etl::byte_stream_writer writer(etl::span<uint8_t>(input_buf + 1, kInputSize - 1),
                                       etl::endian::big);  // leave first byte blank for bit slip pointing later
        auto write_ret = frame_writer::WriteFrame(writer, frame_to_write);
        ASSERT_TRUE(write_ret) << "WriteFrame must succeed to build input for bit-slip test";

        const size_t frame_len = writer.size_bytes();

        // Move frame from (slipped) input_buf to slipped_buf using CopyFromBitSlippedBuffer with bit_slip_count 1.
        etl::byte_stream_reader input_reader(input_buf, kInputSize, etl::endian::big);
        etl::byte_stream_writer slipped_writer(etl::span<uint8_t>(slipped_buf, kBufferSize), etl::endian::big);
        const bool copy_ok = CopyFromBitSlippedBuffer(input_reader, slipped_writer, frame_len,
                                                      7);  // shift left by 7 bits to leave one bit of simulated slip
        ASSERT_TRUE(copy_ok) << "CopyFromBitSlippedBuffer should succeed moving slipped frame into slipped_buf";

        etl::byte_stream_reader slipped_stream(slipped_buf, kInputSize, etl::endian::big);
        FrameBuffer fb(etl::span<uint8_t>(internal_buf, kBufferSize));

        auto ret = fb.CopyToInternalBuffer(slipped_stream, 1);
        ASSERT_TRUE(ret) << "CopyToInternalBuffer should succeed after CopyFromBitSlippedBuffer corrected into "
                            "slipped_buf (error: "
                         << static_cast<int>(ret.error()) << ")";

        Frame& read_frame = fb.GetFrame();
        EXPECT_EQ(read_frame.can_identifier, 0x50U) << "read frame can_identifier should match after slip correction";
        EXPECT_EQ(read_frame.payload.size(), 2U) << "read frame payload size should be 2 after slip correction";
        EXPECT_EQ(read_frame.payload[0], 0xDD) << "payload byte 0 should match after load and read with bit slip";
        EXPECT_EQ(read_frame.payload[1], 0xEE) << "payload byte 1 should match after load and read with bit slip";
        EXPECT_TRUE(read_frame.payload.data() >= internal_buf && read_frame.payload.data() < internal_buf + kBufferSize)
            << "payload should point into internal buffer after CopyToInternalBuffer with bit slip";
    }

    // --- Success: default bit_slip_count (0) ---
    {
        constexpr size_t kInputSize = 32;
        constexpr size_t kBufferSize = 32;
        uint8_t input_buf[kInputSize] = {0};
        uint8_t internal_buf[kBufferSize] = {0};

        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0;
        frame_to_write.payload = etl::span<uint8_t>();

        etl::byte_stream_writer writer(etl::span<uint8_t>(input_buf, kInputSize), etl::endian::big);
        ASSERT_TRUE(frame_writer::WriteFrame(writer, frame_to_write));

        etl::byte_stream_reader input_stream(input_buf, kInputSize, etl::endian::big);
        FrameBuffer fb(etl::span<uint8_t>(internal_buf, kBufferSize));

        auto ret = fb.CopyToInternalBuffer(input_stream);
        ASSERT_TRUE(ret) << "CopyToInternalBuffer should succeed with default bit_slip_count (0)";
        EXPECT_TRUE(fb.GetFrame().payload.empty()) << "read frame payload should be empty for empty-payload frame";
    }

    // --- Failure: input stream too short (no complete frame) ---
    {
        uint8_t input_buf[] = {PREAMBLE_BYTE, PREAMBLE_BYTE};
        uint8_t internal_buf[32] = {0};
        etl::byte_stream_reader input_stream(input_buf, sizeof(input_buf), etl::endian::big);
        FrameBuffer fb(etl::span<uint8_t>(internal_buf, sizeof(internal_buf)));

        auto ret = fb.CopyToInternalBuffer(input_stream, 0);
        EXPECT_FALSE(ret) << "CopyToInternalBuffer should fail when input stream does not contain full frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortToDetermineLength)
                << "error should reflect truncated input when stream too short";
        }
    }

    // --- Failure: internal buffer too short to hold copied frame ---
    {
        constexpr size_t kInputSize = 64;
        uint8_t input_buf[kInputSize] = {0};
        uint8_t internal_buf[kInputSize] = {0};  // too small for any full frame

        Frame frame_to_write{};
        frame_to_write.can_flags = {};
        frame_to_write.can_identifier = 0x100U;
        uint8_t payload_data[] = {0x01, 0x02};
        frame_to_write.payload = etl::span<uint8_t>(payload_data, 2);

        etl::byte_stream_writer writer(etl::span<uint8_t>(input_buf, kInputSize), etl::endian::big);
        ASSERT_TRUE(frame_writer::WriteFrame(writer, frame_to_write));

        etl::byte_stream_reader input_stream(input_buf, kInputSize, etl::endian::big);

        // too short for preamble
        FrameBuffer fb(etl::span<uint8_t>(internal_buf, 1));
        input_stream.restart(0);
        auto ret = fb.CopyToInternalBuffer(input_stream, 0);
        EXPECT_FALSE(ret) << "CopyToInternalBuffer should fail when internal buffer is too short to copy frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortForPreamble);
        }

        // too short to determine length (first part of header)
        fb.SetBuffer(etl::span<uint8_t>(internal_buf, 3));
        input_stream.restart(0);
        ret = fb.CopyToInternalBuffer(input_stream, 0);
        EXPECT_FALSE(ret) << "CopyToInternalBuffer should fail when internal buffer is too short to copy frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortToDetermineLength);
        }

        // too short for header (second part of header)
        fb.SetBuffer(etl::span<uint8_t>(internal_buf, 5));
        input_stream.restart(0);
        ret = fb.CopyToInternalBuffer(input_stream, 0);
        EXPECT_FALSE(ret) << "CopyToInternalBuffer should fail when internal buffer is too short to copy frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortForHeader);
        }

        // too short for payload
        fb.SetBuffer(etl::span<uint8_t>(internal_buf, 7));
        input_stream.restart(0);
        ret = fb.CopyToInternalBuffer(input_stream, 0);
        EXPECT_FALSE(ret) << "CopyToInternalBuffer should fail when internal buffer is too short to copy frame";
        if (!ret) {
            EXPECT_EQ(ret.error(), frame_reader::FrameParseError::BufferTooShortForPayload);
        }
    }
}
