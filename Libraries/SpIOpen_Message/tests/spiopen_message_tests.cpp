#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "spiopen_lifecycle.h"
#include "spiopen_message.h"
#include "spiopen_message_pool.h"
#include "spiopen_message_publisher.h"

using namespace spiopen;
using namespace spiopen::message;

namespace {

// Test-only pool stand-in for FrameMessage unit tests. Overrides RequeueFrameMessage
// to record calls so tests can assert the message requeues to the pool when refcount hits zero.
// Reports Active so AllocateToPublisher accepts the pool; use base FramePool to test InvalidPool.
class FakeFramePool : public FramePool {
   public:
    LifecycleState GetState() const override { return LifecycleState::Active; }

    void RequeueFrameMessage(FrameMessage* message) override {
        ++requeue_count_;
        last_requeued_message_ = message;
    }

    size_t GetRequeueCount() const { return requeue_count_; }
    FrameMessage* GetLastRequeuedMessage() const { return last_requeued_message_; }

   private:
    size_t requeue_count_{0U};
    FrameMessage* last_requeued_message_{nullptr};
};

}  // namespace

TEST(SpIOpen_Message_FrameMessage, Constructor) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        EXPECT_EQ(message.GetState(), FrameMessageState::Available);
        EXPECT_EQ(message.GetReferenceCount(), 0U);
        EXPECT_EQ(message.GetMessageType(), MessageType::None);
        EXPECT_EQ(message.GetPublisherHandle(), nullptr);
    }

    { EXPECT_EQ(&message.GetOwningPool(), &pool); }

    // Verify the storage passed in is the underlying storage of the internal frame buffer
    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto mutable_buf = message.GetMutableFrameBuffer();
        ASSERT_TRUE(mutable_buf) << "GetMutableFrameBuffer should succeed when Allocated";
        etl::span<uint8_t> internal_buffer = (*mutable_buf)->GetBuffer();
        EXPECT_EQ(internal_buffer.data(), storage.data());
        EXPECT_EQ(internal_buffer.size(), storage.size());
    }
}

TEST(SpIOpen_Message_FrameMessage, AllocateToPublisher) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));
    publisher::FramePublisherHandle_t publisher{"unit-test", 0x101U, nullptr};

    {
        auto ret = message.AllocateToPublisher(MessageType::MasterToSlave, &publisher, 1U);
        ASSERT_TRUE(ret) << "AllocateToPublisher should succeed from Available";
        EXPECT_EQ(message.GetState(), FrameMessageState::Allocated);
        EXPECT_EQ(message.GetReferenceCount(), 1U);
        EXPECT_EQ(message.GetMessageType(), MessageType::MasterToSlave);
        EXPECT_EQ(message.GetPublisherHandle(), &publisher);
    }

    {
        auto ret = message.AllocateToPublisher(MessageType::SlaveToMaster, nullptr, 1U);
        EXPECT_FALSE(ret) << "AllocateToPublisher should fail from non-Available state";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::InvalidStateForPublish);
        }
    }

    {
        std::array<uint8_t, 64U> fresh_storage = {};
        FrameMessage fresh_message(pool, etl::span<uint8_t>(fresh_storage.data(), fresh_storage.size()));
        auto ret = fresh_message.AllocateToPublisher(MessageType::MasterToSlave, &publisher, 0U);
        EXPECT_FALSE(ret) << "AllocateToPublisher should reject zero initial references";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::ReferenceUnderflow);
        }
    }

    {
        FramePool fpool;
        std::array<uint8_t, 64U> storage = {};
        FrameMessage message2(fpool, etl::span<uint8_t>(storage.data(), storage.size()));

        auto ret = message2.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U);
        ASSERT_FALSE(ret) << "AllocateToPublisher should fail when pool is not Active";
        EXPECT_EQ(ret.error(), FrameMessageError::InvalidPool);
    }
}

TEST(SpIOpen_Message_FrameMessage, LockForPublishing) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        auto ret = message.LockForPublishing();
        EXPECT_FALSE(ret) << "LockForPublishing should fail from Available";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::InvalidStateForPublish);
        }
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto ret = message.LockForPublishing();
        ASSERT_TRUE(ret) << "LockForPublishing should succeed from Allocated";
        EXPECT_EQ(message.GetState(), FrameMessageState::Published);
        EXPECT_EQ(message.GetReferenceCount(), 1U);
    }

    {
        auto ret = message.LockForPublishing();
        EXPECT_FALSE(ret) << "LockForPublishing should fail from Published";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::InvalidStateForPublish);
        }
    }
}

TEST(SpIOpen_Message_FrameMessage, AcquireReference) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        auto ret = message.AcquireReference();
        EXPECT_FALSE(ret) << "AcquireReference should fail from Available";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::InvalidStateForPublish);
        }
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto ret = message.AcquireReference();
        ASSERT_TRUE(ret) << "AcquireReference should succeed while Allocated";
        EXPECT_EQ(message.GetReferenceCount(), 2U);
    }

    {
        ASSERT_TRUE(message.LockForPublishing());
        auto ret = message.AcquireReference();
        ASSERT_TRUE(ret) << "AcquireReference should succeed while Published";
        EXPECT_EQ(message.GetReferenceCount(), 3U);
    }

    {
        std::array<uint8_t, 64U> overflow_storage = {};
        FrameMessage overflow_message(pool, etl::span<uint8_t>(overflow_storage.data(), overflow_storage.size()));
        ASSERT_TRUE(overflow_message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, UINT8_MAX));
        auto ret = overflow_message.AcquireReference();
        EXPECT_FALSE(ret) << "AcquireReference should fail at uint8_t max";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::ReferenceOverflow);
        }
    }
}

TEST(SpIOpen_Message_FrameMessage, GetMutableFrame) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        auto ret = message.GetMutableFrame();
        EXPECT_FALSE(ret) << "GetMutableFrame should fail from Available";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::NotMutable);
        }
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto ret = message.GetMutableFrame();
        ASSERT_TRUE(ret) << "GetMutableFrame should succeed from Allocated";
        (*ret)->can_identifier = 0x123U;
        EXPECT_EQ(message.GetFrame().can_identifier, 0x123U);
    }

    {
        ASSERT_TRUE(message.LockForPublishing());
        auto ret = message.GetMutableFrame();
        EXPECT_FALSE(ret) << "GetMutableFrame should fail from Published";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::NotMutable);
        }
    }
}

TEST(SpIOpen_Message_FrameMessage, GetMutableFrameBuffer) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        auto ret = message.GetMutableFrameBuffer();
        EXPECT_FALSE(ret) << "GetMutableFrameBuffer should fail from Available";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::NotMutable);
        }
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto ret = message.GetMutableFrameBuffer();
        ASSERT_TRUE(ret) << "GetMutableFrameBuffer should succeed from Allocated";
        (*ret)->GetFrame().can_identifier = 0x456U;
        EXPECT_EQ(message.GetFrame().can_identifier, 0x456U);
    }

    {
        ASSERT_TRUE(message.LockForPublishing());
        auto ret = message.GetMutableFrameBuffer();
        EXPECT_FALSE(ret) << "GetMutableFrameBuffer should fail from Published";
        if (!ret) {
            EXPECT_EQ(ret.error(), FrameMessageError::NotMutable);
        }
    }
}

TEST(SpIOpen_Message_FrameMessage, GetFrame) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        const Frame& frame = message.GetFrame();
        EXPECT_EQ(frame.can_identifier, 0U);
        EXPECT_EQ(frame.payload.size(), 0U);
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto mutable_frame = message.GetMutableFrame();
        ASSERT_TRUE(mutable_frame);
        (*mutable_frame)->can_identifier = 0x222U;

        const Frame& frame = message.GetFrame();
        EXPECT_EQ(frame.can_identifier, 0x222U);
    }
}

TEST(SpIOpen_Message_FrameMessage, GetFrameBuffer) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        const FrameBuffer& frame_buffer = message.GetFrameBuffer();
        EXPECT_EQ(&frame_buffer, &message.GetFrameBuffer());
        EXPECT_EQ(message.GetFrame().can_identifier, 0U);
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        auto mutable_frame_buffer = message.GetMutableFrameBuffer();
        ASSERT_TRUE(mutable_frame_buffer);
        (*mutable_frame_buffer)->GetFrame().can_identifier = 0x333U;

        const FrameBuffer& frame_buffer = message.GetFrameBuffer();
        EXPECT_EQ(&frame_buffer, &message.GetFrameBuffer());
        EXPECT_EQ(message.GetFrame().can_identifier, 0x333U);
    }
}

TEST(SpIOpen_Message_FrameMessage, GetMessageType) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    { EXPECT_EQ(message.GetMessageType(), MessageType::None); }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::SlaveToMaster, nullptr, 1U));
        EXPECT_EQ(message.GetMessageType(), MessageType::SlaveToMaster);
    }
}

TEST(SpIOpen_Message_FrameMessage, GetPublisherHandle) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));
    publisher::FramePublisherHandle_t publisher{"publisher-a", 0x10U, nullptr};

    { EXPECT_EQ(message.GetPublisherHandle(), nullptr); }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, &publisher, 1U));
        EXPECT_EQ(message.GetPublisherHandle(), &publisher);
    }
}

TEST(SpIOpen_Message_FrameMessage, GetState) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    { EXPECT_EQ(message.GetState(), FrameMessageState::Available); }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        EXPECT_EQ(message.GetState(), FrameMessageState::Allocated);
    }

    {
        ASSERT_TRUE(message.LockForPublishing());
        EXPECT_EQ(message.GetState(), FrameMessageState::Published);
    }
}

TEST(SpIOpen_Message_FrameMessage, GetReferenceCount) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    { EXPECT_EQ(message.GetReferenceCount(), 0U); }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
        EXPECT_EQ(message.GetReferenceCount(), 1U);
        ASSERT_TRUE(message.AcquireReference());
        EXPECT_EQ(message.GetReferenceCount(), 2U);
    }
}

TEST(SpIOpen_Message_FrameMessage, Release) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    {
        message.Release();
        EXPECT_EQ(message.GetReferenceCount(), 0U);
        EXPECT_EQ(message.GetState(), FrameMessageState::Available);
        EXPECT_EQ(pool.GetRequeueCount(), 0U) << "Release from zero refs should not requeue";
    }

    {
        ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 2U));
        ASSERT_TRUE(message.LockForPublishing());
        EXPECT_EQ(message.GetReferenceCount(), 2U);
        EXPECT_EQ(message.GetState(), FrameMessageState::Published);
    }

    {
        message.Release();
        EXPECT_EQ(message.GetReferenceCount(), 1U);
        EXPECT_EQ(message.GetState(), FrameMessageState::Published);
        EXPECT_EQ(pool.GetRequeueCount(), 0U) << "Requeue only on final release";
    }

    {
        message.Release();
        EXPECT_EQ(message.GetReferenceCount(), 0U);
        EXPECT_EQ(message.GetState(), FrameMessageState::Available);
        EXPECT_EQ(message.GetMessageType(), MessageType::None);
        EXPECT_EQ(message.GetPublisherHandle(), nullptr);
        EXPECT_EQ(pool.GetRequeueCount(), 1U) << "Pool requeue called when refcount reached zero";
        EXPECT_EQ(pool.GetLastRequeuedMessage(), &message) << "Requeued message must be this message";
    }
}

TEST(SpIOpen_Message_FrameMessage, GetOwningPool) {
    FakeFramePool pool;
    std::array<uint8_t, 64U> storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(storage.data(), storage.size()));

    { EXPECT_EQ(&message.GetOwningPool(), &pool); }
}
