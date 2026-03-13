#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "spiopen_frame.h"

using namespace spiopen;

TEST(SpIOpen_Frame, Reset) {
    Frame frame;
    // constructor resets everything, but not with the reset function...
    EXPECT_EQ(frame.can_identifier, 0U) << "Frame identifier";
    EXPECT_FALSE(frame.can_flags.IDE) << "Frame IDE flag";
    EXPECT_FALSE(frame.can_flags.TTL) << "Frame TTL flag";
    EXPECT_FALSE(frame.can_flags.XLF) << "Frame XLF flag";
    EXPECT_FALSE(frame.can_flags.WA) << "Frame WA flag";
    EXPECT_FALSE(frame.can_flags.FDF) << "Frame FDF flag";
    EXPECT_FALSE(frame.can_flags.RTR) << "Frame RTR flag";
    EXPECT_FALSE(frame.can_flags.BRS) << "Frame BRS flag";
    EXPECT_FALSE(frame.can_flags.ESI) << "Frame ESI flag";
    EXPECT_EQ(frame.time_to_live, 0U) << "Frame time to live";
    EXPECT_EQ(frame.xl_control.payload_type, 0U) << "Frame XL control payload type";
    EXPECT_EQ(frame.xl_control.virtual_can_network_id, 0U) << "Frame XL control virtual can network id";
    EXPECT_EQ(frame.xl_control.addressing_field, 0U) << "Frame XL control addressing field";
    EXPECT_EQ(frame.payload.size(), 0U) << "Frame payload is empty";

    // set everything!
    frame.can_identifier = 0xFFFFU;
    frame.can_flags.IDE = true;
    frame.can_flags.TTL = true;
    frame.can_flags.XLF = true;
    frame.can_flags.WA = true;
    frame.can_flags.FDF = true;
    frame.can_flags.RTR = true;
    frame.can_flags.BRS = true;
    frame.can_flags.ESI = true;
    frame.time_to_live = 0xFFU;
    frame.xl_control.payload_type = 0xFFU;
    frame.xl_control.virtual_can_network_id = 0xFFU;
    frame.xl_control.addressing_field = 0xFFFFFFFFU;
    etl::span<uint8_t> payload_span(new uint8_t[10U], 10U);
    frame.payload = payload_span;

    // reset should clear everything!
    frame.Reset();
    EXPECT_EQ(frame.can_identifier, 0U) << "Frame identifier";
    EXPECT_FALSE(frame.can_flags.IDE) << "Frame IDE flag";
    EXPECT_FALSE(frame.can_flags.TTL) << "Frame TTL flag";
    EXPECT_FALSE(frame.can_flags.XLF) << "Frame XLF flag";
    EXPECT_FALSE(frame.can_flags.WA) << "Frame WA flag";
    EXPECT_FALSE(frame.can_flags.FDF) << "Frame FDF flag";
    EXPECT_FALSE(frame.can_flags.RTR) << "Frame RTR flag";
    EXPECT_FALSE(frame.can_flags.BRS) << "Frame BRS flag";
    EXPECT_FALSE(frame.can_flags.ESI) << "Frame ESI flag";
    EXPECT_EQ(frame.time_to_live, 0U) << "Frame time to live";
    EXPECT_EQ(frame.xl_control.payload_type, 0U) << "Frame XL control payload type";
    EXPECT_EQ(frame.xl_control.virtual_can_network_id, 0U) << "Frame XL control virtual can network id";
    EXPECT_EQ(frame.xl_control.addressing_field, 0U) << "Frame XL control addressing field";
    EXPECT_EQ(frame.payload.size(), 0U) << "Frame payload is empty";
}

TEST(SpIOpen_Frame, HeaderLength) {
    Frame frame;
    EXPECT_EQ(frame.GetHeaderLength(), 4U) << "Empty frame";

    frame.Reset();
    frame.can_flags.IDE = true;
    EXPECT_EQ(frame.GetHeaderLength(), 6U) << "Frame with IDE flag";

    frame.Reset();
    frame.can_flags.TTL = true;
    EXPECT_EQ(frame.GetHeaderLength(), 5U) << "Frame with TTL flag";

#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    frame.Reset();
    frame.can_flags.FDF = true;
    EXPECT_EQ(frame.GetHeaderLength(), 4U) << "Frame with FDF flag";
#endif

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    frame.Reset();
    frame.can_flags.XLF = true;
    EXPECT_EQ(frame.GetHeaderLength(), 12U) << "Frame with XLF flag";
#endif
}

TEST(SpIOpen_Frame, FrameLength) {
    Frame frame;
    uint8_t frame_data[2050U];
    frame.payload = etl::span<uint8_t>(frame_data, 0);
    size_t frame_length = 0U;

    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Empty frame";
    EXPECT_EQ(frame_length, 8U) << "Empty frame";

    frame.Reset();
    frame.payload = etl::span<uint8_t>(frame_data, 1);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 1B payload";
    EXPECT_EQ(frame_length, 9U) << "Frame with 1B payload";
    frame.can_flags.WA = true;
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 1B payload and WA flag";
    EXPECT_EQ(frame_length, 10U) << "Frame with 1B payload and WA flag";
    frame.payload = etl::span<uint8_t>(frame_data, 9);
    EXPECT_FALSE(frame.TryGetFrameLength(frame_length)) << "Frame with 9B payload in CC mode";

#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    frame.Reset();
    frame.payload = etl::span<uint8_t>(frame_data, 8);
    frame.can_flags.FDF = true;
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 8B payload and FDF flag";
    EXPECT_EQ(frame_length, 16U) << "Frame with 8B payload and FDF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 12);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 12B payload and FDF flag";
    EXPECT_EQ(frame_length, 22U) << "Frame with 12B payload and FDF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 14);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 14B payload + padding and FDF flag";
    EXPECT_EQ(frame_length, 26U) << "Frame with 14B payload + padding and FDF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 64);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 64B payload and FDF flag";
    EXPECT_EQ(frame_length, 74U) << "Frame with 64B payload and FDF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 65);
    EXPECT_FALSE(frame.TryGetFrameLength(frame_length)) << "Frame with 65B payload and FDF flag";
#endif

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    frame.Reset();
    frame.payload = etl::span<uint8_t>(frame_data, 8);
    frame.can_flags.XLF = true;
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 8B payload and XLF flag";
    EXPECT_EQ(frame_length, 24U) << "Frame with 8B payload and XLF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 12);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 12B payload and XLF flag";
    EXPECT_EQ(frame_length, 30U) << "Frame with 12B payload and XLF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 64);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 64B payload and XLF flag";
    EXPECT_EQ(frame_length, 82U) << "Frame with 64B payload and XLF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 2048);
    EXPECT_TRUE(frame.TryGetFrameLength(frame_length)) << "Frame with 2048B payload and XLF flag";
    EXPECT_EQ(frame_length, 2066U) << "Frame with 2048B payload and XLF flag";
    frame.payload = etl::span<uint8_t>(frame_data, 2049);
    EXPECT_FALSE(frame.TryGetFrameLength(frame_length)) << "Frame with 2049B payload and XLF flag";
#endif
}

TEST(SpIOpen_Frame, DecrementAndCheckIfTimeToLiveExpired) {
    Frame frame;
    EXPECT_FALSE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Empty frame";
    frame.can_flags.TTL = true;
    EXPECT_TRUE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Frame with TTL flag and uninitialzied TTL";
    frame.time_to_live = 2U;
    EXPECT_FALSE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Frame with TTL flag and TTL=2";
    EXPECT_TRUE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Frame with TTL flag and TTL=1";
    EXPECT_TRUE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Frame with TTL flag and TTL=0";
    frame.Reset();
    frame.time_to_live = 2U;
    EXPECT_FALSE(frame.DecrementAndCheckIfTimeToLiveExpired()) << "Frame without TTL flag and TTL=2";
    EXPECT_EQ(frame.time_to_live, 2U) << "Frame should not decrement if TTL flag is not set";
}

TEST(SpIOpen_Frame, GetCanMessageTypeFromFlags) {
    Frame frame;

    frame.Reset();
    EXPECT_EQ(frame.GetCanMessageType(), format::CanMessageType::CanCc);

    frame.Reset();
    frame.can_flags.FDF = true;
    EXPECT_EQ(frame.GetCanMessageType(), format::CanMessageType::CanFd);

    frame.Reset();
    frame.can_flags.XLF = true;
    EXPECT_EQ(frame.GetCanMessageType(), format::CanMessageType::CanXl);

    frame.Reset();
    frame.can_flags.FDF = true;
    frame.can_flags.XLF = true;
    EXPECT_EQ(frame.GetCanMessageType(), format::CanMessageType::CanXl)
        << "XLF should take priority over FDF when both are set";
}