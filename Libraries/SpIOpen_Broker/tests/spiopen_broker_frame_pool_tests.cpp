#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "spiopen_broker_frame_pool.h"
#include "spiopen_broker_frame_publisher.h"
#include "spiopen_broker_lifecycle.h"
#include "spiopen_frame_format.h"

using namespace spiopen;
using namespace spiopen::broker;

namespace {

constexpr size_t kCcMessageCount = 2U;
constexpr size_t kCcPoolBytes = BytesToAllocateForFramePool(kCcMessageCount, format::CanMessageType::CanCc);

size_t GetAllocatedMessageBufferSize(FrameMessage* message) {
    auto mutable_buffer = message->GetMutableFrameBuffer();
    if (!mutable_buffer) {
        return 0U;
    }
    return (*mutable_buffer)->GetBuffer().size();
}

}  // namespace

TEST(SpIOpen_Broker_FramePool, Constructor) {
    FramePool pool;

    EXPECT_EQ(pool.GetState(), LifecycleState::Unconfigured);
    EXPECT_EQ(pool.GetMessageCount(), 0U);
    EXPECT_EQ(pool.GetFrameBufferSize(), 0U);
}

TEST(SpIOpen_Broker_FramePool, BytesToAllocateForFramePool) {
    const size_t message_count = 3U;
    const size_t queue_storage_size = message_count * sizeof(FrameMessage*);
    const size_t alignment = alignof(FrameMessage);
    const size_t messages_offset =
        (alignment == 0U) ? queue_storage_size : ((queue_storage_size + alignment - 1U) / alignment) * alignment;
    const size_t messages_storage_size = message_count * sizeof(FrameMessage);

    {
        const size_t expected = messages_offset + messages_storage_size +
                                (message_count * format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[static_cast<size_t>(
                                                     format::CanMessageType::CanCc)]);
        EXPECT_EQ(BytesToAllocateForFramePool(message_count, format::CanMessageType::CanCc), expected);
    }

    {
        const size_t expected = messages_offset + messages_storage_size +
                                (message_count * format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[static_cast<size_t>(
                                                     format::CanMessageType::CanFd)]);
        EXPECT_EQ(BytesToAllocateForFramePool(message_count, format::CanMessageType::CanFd), expected);
    }

    {
        const size_t expected = messages_offset + messages_storage_size +
                                (message_count * format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[static_cast<size_t>(
                                                     format::CanMessageType::CanXl)]);
        EXPECT_EQ(BytesToAllocateForFramePool(message_count, format::CanMessageType::CanXl), expected);
    }

    EXPECT_EQ(BytesToAllocateForFramePool(0U, format::CanMessageType::CanCc), 0U);
}

TEST(SpIOpen_Broker_FramePool, ConfigureAndLifecycle) {
    FramePool pool;
    std::array<uint8_t, kCcPoolBytes> pool_storage{};
    FramePoolConfig config{
        kCcMessageCount, format::MAX_CAN_CC_FRAME_SIZE, etl::span<uint8_t>(pool_storage.data(), pool_storage.size())};

    ASSERT_TRUE(pool.Configure(config));
    EXPECT_EQ(pool.GetState(), LifecycleState::Configured);
    EXPECT_EQ(pool.GetMessageCount(), kCcMessageCount);
    EXPECT_EQ(pool.GetFrameBufferSize(), format::MAX_CAN_CC_FRAME_SIZE);

    ASSERT_TRUE(pool.Initialize());
    EXPECT_EQ(pool.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(pool.Start());
    EXPECT_EQ(pool.GetState(), LifecycleState::Active);

    ASSERT_TRUE(pool.Stop());
    EXPECT_EQ(pool.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(pool.Deinitialize());
    EXPECT_EQ(pool.GetState(), LifecycleState::Configured);

    auto reset_ret = pool.Reset();
    ASSERT_TRUE(reset_ret);
    EXPECT_EQ(pool.GetState(), LifecycleState::Unconfigured);
}

TEST(SpIOpen_Broker_FramePool, ConfigureRejectsInsufficientStorage) {
    FramePool pool;
    constexpr size_t kOneMessageCount = 1U;
    constexpr size_t kRequiredBytes = BytesToAllocateForFramePool(kOneMessageCount, format::CanMessageType::CanCc);
    static_assert(kRequiredBytes > 0U);
    std::array<uint8_t, kRequiredBytes - 1U> too_small_storage{};

    FramePoolConfig config{kOneMessageCount, format::MAX_CAN_CC_FRAME_SIZE,
                           etl::span<uint8_t>(too_small_storage.data(), too_small_storage.size())};

    auto ret = pool.Configure(config);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidArgument);
    }
}

TEST(SpIOpen_Broker_FramePool, AllocateRequiresActiveState) {
    FramePool pool;
    std::array<uint8_t, kCcPoolBytes> pool_storage{};
    FramePoolConfig config{
        kCcMessageCount, format::MAX_CAN_CC_FRAME_SIZE, etl::span<uint8_t>(pool_storage.data(), pool_storage.size())};
    ASSERT_TRUE(pool.Configure(config));

    {
        auto ret = pool.AllocateFrameMessage(message::MessageType::MasterToSlave, nullptr, 0U);
        EXPECT_FALSE(ret);
        if (!ret) {
            EXPECT_EQ(ret.error(), FramePoolError::NotInitialized);
        }
    }

    ASSERT_TRUE(pool.Initialize());
    {
        auto ret = pool.AllocateFrameMessage(message::MessageType::MasterToSlave, nullptr, 0U);
        EXPECT_FALSE(ret);
        if (!ret) {
            EXPECT_EQ(ret.error(), FramePoolError::NotActive);
        }
    }
}

TEST(SpIOpen_Broker_FramePool, AllocateReturnsMetadataAndPoolExhaustedThenRecycle) {
    constexpr size_t kOneMessageCount = 1U;
    constexpr size_t kOnePoolBytes = BytesToAllocateForFramePool(kOneMessageCount, format::CanMessageType::CanCc);

    FramePool pool;
    std::array<uint8_t, kOnePoolBytes> pool_storage{};
    FramePoolConfig config{
        kOneMessageCount, format::MAX_CAN_CC_FRAME_SIZE, etl::span<uint8_t>(pool_storage.data(), pool_storage.size())};
    ASSERT_TRUE(pool.Configure(config));
    ASSERT_TRUE(pool.Initialize());
    ASSERT_TRUE(pool.Start());

    publisher::FramePublisherHandle_t publisher{"pool-test-publisher", 0x77U, nullptr};

    auto first = pool.AllocateFrameMessage(message::MessageType::MasterToSlave, &publisher, 0U);
    ASSERT_TRUE(first);
    ASSERT_NE(*first, nullptr);
    EXPECT_EQ((*first)->GetOwningPool().GetState(), pool.GetState());
    EXPECT_EQ((*first)->GetMessageType(), message::MessageType::MasterToSlave);
    EXPECT_EQ((*first)->GetPublisherHandle(), &publisher);
    EXPECT_EQ((*first)->GetState(), FrameMessageState::Allocated);
    EXPECT_EQ((*first)->GetReferenceCount(), 1U);
    EXPECT_EQ(GetAllocatedMessageBufferSize(*first), format::MAX_CAN_CC_FRAME_SIZE);

    auto second = pool.AllocateFrameMessage(message::MessageType::MasterToSlave, &publisher, 0U);
    EXPECT_FALSE(second);
    if (!second) {
        EXPECT_EQ(second.error(), FramePoolError::PoolExhausted);
    }

    (*first)->Release();

    auto third = pool.AllocateFrameMessage(message::MessageType::SlaveToMaster, nullptr, 0U);
    ASSERT_TRUE(third);
    EXPECT_EQ(*third, *first) << "Single-message pool should recycle the same message object";
    EXPECT_EQ((*third)->GetMessageType(), message::MessageType::SlaveToMaster);
    EXPECT_EQ((*third)->GetPublisherHandle(), nullptr);
}
