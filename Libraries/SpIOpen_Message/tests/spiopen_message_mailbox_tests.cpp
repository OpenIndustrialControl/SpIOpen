#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "spiopen_message_mailbox.h"
#include "spiopen_message_pool.h"
#include "spiopen_message_publisher.h"

using namespace spiopen;
using namespace spiopen::message;

namespace {

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

TEST(SpIOpen_Message_FrameMailbox, Constructor) {
    FrameMailbox mailbox;

    EXPECT_EQ(mailbox.GetState(), LifecycleState::Unconfigured);
}

TEST(SpIOpen_Message_FrameMailbox, Configure) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};

    auto ret = mailbox.Configure(config);
    ASSERT_TRUE(ret) << "Configure should accept non-zero queue depth";
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Configured);
}

TEST(SpIOpen_Message_FrameMailbox, ConfigureRejectsInvalidArguments) {
    FrameMailbox mailbox;
    FrameMailboxConfig config{0U, etl::span<uint8_t>()};

    auto ret = mailbox.Configure(config);
    EXPECT_FALSE(ret) << "Configure should reject zero-depth mailbox configuration";
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidConfiguration);
    }
}

TEST(SpIOpen_Message_FrameMailbox, Lifecycle) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};

    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Inactive);
    ASSERT_TRUE(mailbox.Start());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Active);
    ASSERT_TRUE(mailbox.Stop());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Inactive);
    ASSERT_TRUE(mailbox.Deinitialize());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Configured);
}

TEST(SpIOpen_Message_FrameMailbox, UnconfigureFromConfigured) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size()), "mailbox-reset"};

    ASSERT_TRUE(mailbox.Configure(config));
    auto ret = mailbox.Unconfigure();
    ASSERT_TRUE(ret) << "Unconfigure should succeed from Configured state";
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Unconfigured);
}

TEST(SpIOpen_Message_FrameMailbox, UnconfigureInvalidState) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};

    {
        auto ret = mailbox.Unconfigure();
        EXPECT_FALSE(ret) << "Unconfigure should fail when already Unconfigured";
        if (!ret) {
            EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidState);
        }
    }

    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    {
        auto ret = mailbox.Unconfigure();
        EXPECT_FALSE(ret) << "Unconfigure should fail while initialized";
        if (!ret) {
            EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidState);
        }
    }
}

TEST(SpIOpen_Message_FrameMailbox, EnqueueRequiresActiveState) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Inactive);

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));

    auto ret = mailbox.Enqueue(&message, 0U);
    EXPECT_FALSE(ret) << "Enqueue should fail while mailbox is Inactive";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameMailboxError::InvalidState);
    }
}

TEST(SpIOpen_Message_FrameMailbox, EnqueueRejectsNullMessage) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    ASSERT_TRUE(mailbox.Start());

    auto ret = mailbox.Enqueue(nullptr, 0U);
    EXPECT_FALSE(ret) << "Enqueue should reject null message pointer";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameMailboxError::InvalidArgument);
    }
}

TEST(SpIOpen_Message_FrameMailbox, DequeueAllowedWhileInactive) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Inactive);

    auto ret = mailbox.Dequeue(0U);
    EXPECT_FALSE(ret) << "Dequeue on empty inactive mailbox should fail non-blocking";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameMailboxError::QueueTimeout);
    }
}

TEST(SpIOpen_Message_FrameMailbox, DrainAndReleaseAllAllowedWhileInactive) {
    FrameMailbox mailbox;
    std::array<uint8_t, 8U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{8U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    EXPECT_EQ(mailbox.GetState(), LifecycleState::Inactive);

    auto ret = mailbox.DrainAndReleaseAll();
    ASSERT_TRUE(ret) << "Drain should be valid in Inactive mode";
    EXPECT_EQ(*ret, 0U);
}

TEST(SpIOpen_Message_FrameMailbox, EnqueueAcquireAndRollbackOnFailure) {
    FrameMailbox mailbox;
    std::array<uint8_t, 1U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{1U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    ASSERT_TRUE(mailbox.Start());

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));

    auto first = mailbox.Enqueue(&message, 0U);
    ASSERT_TRUE(first) << "First enqueue should succeed into empty depth-1 mailbox";
    EXPECT_EQ(message.GetReferenceCount(), 2U);

    auto second = mailbox.Enqueue(&message, 0U);
    EXPECT_FALSE(second) << "Second enqueue should fail when queue is full";
    if (!second) {
        EXPECT_EQ(second.error(), FrameMailboxError::QueueTimeout);
    }
    EXPECT_EQ(message.GetReferenceCount(), 2U) << "Failed enqueue should roll back acquired reference";
}

TEST(SpIOpen_Message_FrameMailbox, DequeueIsMoveAndDoesNotReleaseReference) {
    FrameMailbox mailbox;
    std::array<uint8_t, 1U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{1U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    ASSERT_TRUE(mailbox.Start());

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));

    ASSERT_TRUE(mailbox.Enqueue(&message, 0U));
    EXPECT_EQ(message.GetReferenceCount(), 2U);

    auto ret = mailbox.Dequeue(0U);
    ASSERT_TRUE(ret) << "Dequeue should return queued message";
    EXPECT_EQ(*ret, &message);
    EXPECT_EQ(message.GetReferenceCount(), 2U) << "Dequeue should not decrement message reference";
}

TEST(SpIOpen_Message_FrameMailbox, DeinitializeFailsWhenMailboxIsNotEmpty) {
    FrameMailbox mailbox;
    std::array<uint8_t, 1U * sizeof(FrameMessage*)> queue_storage = {};
    FrameMailboxConfig config{1U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
    ASSERT_TRUE(mailbox.Configure(config));
    ASSERT_TRUE(mailbox.Initialize());
    ASSERT_TRUE(mailbox.Start());

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage = {};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
    ASSERT_TRUE(mailbox.Enqueue(&message, 0U));

    ASSERT_TRUE(mailbox.Stop());
    auto ret = mailbox.Deinitialize();
    EXPECT_FALSE(ret) << "Deinitialize should fail while mailbox queue still contains messages";
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::ResourceFailure);
    }
}
