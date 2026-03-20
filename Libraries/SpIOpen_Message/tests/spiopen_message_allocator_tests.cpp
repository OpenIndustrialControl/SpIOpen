#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "spiopen_frame_format.h"
#include "spiopen_lifecycle.h"
#include "spiopen_message_allocator.h"
#include "spiopen_message_publisher.h"

using namespace spiopen;
using namespace spiopen::message;

namespace {

struct AllocatorHarness {
#if MESSAGE_FRAME_POOL_SIZE_CONFIGURABLE
    static constexpr size_t kCcCount = 2U;
#else
    static constexpr size_t kCcCount = MESSAGE_FRAME_POOL_MAX_CC_FRAMES;
#endif
    static constexpr size_t kCcBytes = BytesToAllocateForFramePool(kCcCount, format::CanMessageType::CanCc);
#if MESSAGE_FRAME_POOL_SIZE_CONFIGURABLE
    static constexpr size_t kFdCount = 2U;
#else
    static constexpr size_t kFdCount = MESSAGE_FRAME_POOL_MAX_FD_FRAMES;
#endif
    static constexpr size_t kFdBytes = BytesToAllocateForFramePool(kFdCount, format::CanMessageType::CanFd);
#if MESSAGE_FRAME_POOL_SIZE_CONFIGURABLE
    static constexpr size_t kXlCount = MESSAGE_CAN_XL_ENABLED ? 2U : 0U;
#else
    static constexpr size_t kXlCount = MESSAGE_CAN_XL_ENABLED ? MESSAGE_FRAME_POOL_MAX_XL_FRAMES : 0U;
#endif
    static constexpr size_t kXlBytes = BytesToAllocateForFramePool(kXlCount, format::CanMessageType::CanXl);

    std::array<uint8_t, kCcBytes> cc_storage{};
    FramePool cc_pool;
    std::array<uint8_t, kFdBytes> fd_storage{};
    FramePool fd_pool;
    std::array<uint8_t, kXlBytes> xl_storage{};
    FramePool xl_pool;

    FrameMessageAllocator allocator;
    FramePoolConfig cc_config;
    FramePoolConfig fd_config;
    FramePoolConfig xl_config;

    AllocatorHarness()
        : cc_storage{},
          cc_pool{},
          fd_storage{},
          fd_pool{},
          xl_storage{},
          xl_pool{},
          allocator(cc_pool, fd_pool, xl_pool),
          cc_config{kCcCount, format::MAX_CAN_CC_FRAME_SIZE, etl::span<uint8_t>(cc_storage.data(), cc_storage.size())},
          fd_config{kFdCount, format::MAX_CAN_FD_FRAME_SIZE, etl::span<uint8_t>(fd_storage.data(), fd_storage.size())},
          xl_config{kXlCount, format::MAX_CAN_XL_FRAME_SIZE, etl::span<uint8_t>(xl_storage.data(), xl_storage.size())} {
    }

    etl::expected<void, FrameMessageAllocator::ErrorType> ConfigureAll() {
        return allocator.Configure(FrameMessageAllocator::ConfigType{
            NoLocalConfig{}, FrameMessageAllocator::AggregateBase::ChildConfigTuple{cc_config, fd_config, xl_config}});
    }
};

size_t GetAllocatedMessageBufferSize(FrameMessage* message) {
    auto mutable_buffer = message->GetMutableFrameBuffer();
    if (!mutable_buffer) {
        return 0U;
    }
    return (*mutable_buffer)->GetBuffer().size();
}

}  // namespace

TEST(SpIOpen_Message_FrameAllocator, LifecycleTransitionsAcrossPools) {
    AllocatorHarness harness;

    ASSERT_TRUE(harness.ConfigureAll());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(harness.allocator.Initialize());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(harness.allocator.Start());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Active);

    ASSERT_TRUE(harness.allocator.Stop());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(harness.allocator.Deinitialize());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(harness.allocator.Unconfigure());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Unconfigured);
}

TEST(SpIOpen_Message_FrameAllocator, AllocateRoutesByPayloadSize) {
    AllocatorHarness harness;
    ASSERT_TRUE(harness.ConfigureAll());
    ASSERT_TRUE(harness.allocator.Initialize());
    ASSERT_TRUE(harness.allocator.Start());

    {
        auto ret = harness.allocator.AllocateFrameMessage(8U, MessageType::MasterToSlave, nullptr, 0U);
        ASSERT_TRUE(ret);
        EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_CC_FRAME_SIZE);
        (*ret)->Release();
    }

    {
        auto ret = harness.allocator.AllocateFrameMessage(9U, MessageType::MasterToSlave, nullptr, 0U);
        ASSERT_TRUE(ret);
        EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_FD_FRAME_SIZE);
        (*ret)->Release();
    }

    {
        auto ret = harness.allocator.AllocateFrameMessage(65U, MessageType::MasterToSlave, nullptr, 0U);
        if (MESSAGE_CAN_XL_ENABLED) {
            ASSERT_TRUE(ret);
            EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_XL_FRAME_SIZE);
            (*ret)->Release();
        } else {
            EXPECT_FALSE(ret);
            if (!ret) {
                EXPECT_EQ(ret.error(), FramePoolError::UnsupportedFrameSize);
            }
        }
    }
}

TEST(SpIOpen_Message_FrameAllocator, AllocateByTypeApis) {
    AllocatorHarness harness;
    ASSERT_TRUE(harness.ConfigureAll());
    ASSERT_TRUE(harness.allocator.Initialize());
    ASSERT_TRUE(harness.allocator.Start());

    {
        auto ret = harness.allocator.AllocateFrameMessage(format::CanMessageType::CanCc, MessageType::MasterToSlave,
                                                          nullptr, 0U);
        ASSERT_TRUE(ret);
        EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_CC_FRAME_SIZE);
        (*ret)->Release();
    }

    {
        auto ret = harness.allocator.AllocateFrameMessage(format::CanMessageType::CanFd, MessageType::MasterToSlave,
                                                          nullptr, 0U);
        ASSERT_TRUE(ret);
        EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_FD_FRAME_SIZE);
        (*ret)->Release();
    }

    {
        auto ret = harness.allocator.AllocateFrameMessage(format::CanMessageType::CanXl, MessageType::MasterToSlave,
                                                          nullptr, 0U);
        if (MESSAGE_CAN_XL_ENABLED) {
            ASSERT_TRUE(ret);
            EXPECT_EQ(GetAllocatedMessageBufferSize(*ret), format::MAX_CAN_XL_FRAME_SIZE);
            (*ret)->Release();
        } else {
            EXPECT_FALSE(ret);
            if (!ret) {
                EXPECT_EQ(ret.error(), FramePoolError::UnsupportedFrameSize);
            }
        }
    }
}

TEST(SpIOpen_Message_FrameAllocator, AllocateRejectsUnsupportedPayloadSize) {
    AllocatorHarness harness;
    ASSERT_TRUE(harness.ConfigureAll());
    ASSERT_TRUE(harness.allocator.Initialize());
    ASSERT_TRUE(harness.allocator.Start());

    constexpr size_t kTooLargePayload = format::MAX_XL_PAYLOAD_SIZE + 1U;
    auto ret = harness.allocator.AllocateFrameMessage(kTooLargePayload, MessageType::MasterToSlave, nullptr, 0U);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error(), FramePoolError::UnsupportedFrameSize);
    }
}

TEST(SpIOpen_Message_FrameAllocator, UnconfigureAfterFullLifecycle) {
    AllocatorHarness harness;
    ASSERT_TRUE(harness.ConfigureAll());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(harness.allocator.Initialize());
    ASSERT_TRUE(harness.allocator.Start());
    ASSERT_TRUE(harness.allocator.Stop());
    ASSERT_TRUE(harness.allocator.Deinitialize());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(harness.allocator.Unconfigure());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Unconfigured);

    ASSERT_TRUE(harness.ConfigureAll());
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Configured);
}

TEST(SpIOpen_Message_FrameAllocator, ConfigureValidationRejectsInvalidFrameBufferSize) {
    AllocatorHarness harness;
    FramePoolConfig bad_cc_config = harness.cc_config;
    bad_cc_config.frame_buffer_size = 0U;

    auto ret = harness.allocator.Configure(FrameMessageAllocator::ConfigType{
        NoLocalConfig{},
        FrameMessageAllocator::AggregateBase::ChildConfigTuple{bad_cc_config, harness.fd_config, harness.xl_config}});
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidConfiguration);
    }
    EXPECT_EQ(harness.allocator.GetState(), LifecycleState::Unconfigured);
}
