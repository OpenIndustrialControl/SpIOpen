#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "spiopen_message_broker.h"
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

   private:
    size_t requeue_count_{0U};
    FrameMessage* last_requeued_message_{nullptr};
};

constexpr size_t kInboxDepth = 8U;
constexpr size_t kInboxStorageSize = kInboxDepth * sizeof(FrameMessage*);
constexpr size_t kThreadStackBytes = MESSAGE_THREAD_MAX_STACK_SIZE;

struct BrokerHarness {
    std::array<uint8_t, kInboxStorageSize> inbox_storage{};
    std::array<uint8_t, kThreadStackBytes> thread_stack_storage{};
    FrameBroker broker;
    FrameBrokerConfig config;

    BrokerHarness()
        : inbox_storage{},
          thread_stack_storage{},
          broker{},
          config{"test-broker", osPriorityNormal, static_cast<uint32_t>(kThreadStackBytes),
                 etl::span<uint8_t>(thread_stack_storage.data(), thread_stack_storage.size()),
                 FrameMailboxConfig{kInboxDepth, etl::span<uint8_t>(inbox_storage.data(), inbox_storage.size()),
                                    "broker-inbox"}} {}

    etl::expected<void, LifecycleError> ConfigureAll() { return broker.Configure(config); }
};

struct SubscriberHarness {
    std::array<uint8_t, 4U * sizeof(FrameMessage*)> queue_storage{};
    FrameMailbox mailbox;
    subscriber::FrameSubscriberHandle_t handle;

    SubscriberHarness(const char* name, uint16_t filter_mask)
        : queue_storage{}, mailbox{}, handle{name, &mailbox, {filter_mask}, {0U}} {}

    bool InitializeMailbox() {
        FrameMailboxConfig config{4U, etl::span<uint8_t>(queue_storage.data(), queue_storage.size())};
        auto ret = mailbox.Configure(config);
        if (!ret) return false;
        ret = mailbox.Initialize();
        if (!ret) return false;
        ret = mailbox.Start();
        return ret.has_value();
    }
};

}  // namespace

TEST(SpIOpen_Message_FrameBroker, Constructor) {
    FrameBroker broker;
    EXPECT_EQ(broker.GetState(), LifecycleState::Unconfigured);
    EXPECT_EQ(broker.GetEnqueueErrorCount(), 0U);
}

TEST(SpIOpen_Message_FrameBroker, ConfigureAcceptsValidConfig) {
    BrokerHarness h;
    auto ret = h.ConfigureAll();
    ASSERT_TRUE(ret);
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Configured);
}

TEST(SpIOpen_Message_FrameBroker, ConfigureRejectsInvalidArgs) {
    {
        BrokerHarness h;
        h.config.thread_stack_size_bytes = 0U;
        auto ret = h.broker.Configure(h.config);
        EXPECT_FALSE(ret) << "Should reject zero stack size";
        if (!ret) {
            EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidConfiguration);
        }
        EXPECT_EQ(h.broker.GetState(), LifecycleState::Unconfigured);
    }
    {
        BrokerHarness h;
        h.config.inbox_mailbox_config.depth = 0U;
        auto ret = h.broker.Configure(h.config);
        EXPECT_FALSE(ret) << "Should reject zero inbox depth";
        EXPECT_EQ(h.broker.GetState(), LifecycleState::Unconfigured);
    }
    {
        BrokerHarness h;
        h.config.thread_stack_size_bytes = static_cast<uint32_t>(MESSAGE_THREAD_MAX_STACK_SIZE + 1U);
        auto ret = h.broker.Configure(h.config);
        EXPECT_FALSE(ret) << "Should reject stack size above max";
        EXPECT_EQ(h.broker.GetState(), LifecycleState::Unconfigured);
    }
}

TEST(SpIOpen_Message_FrameBroker, ConfigureRequiresExternalStackWhenNotConfigurable) {
    if constexpr (!MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT) {
        BrokerHarness h;
        h.config.thread_stack_storage = etl::span<uint8_t>();
        auto ret = h.broker.Configure(h.config);
        EXPECT_FALSE(ret);
        if (!ret) {
            EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidConfiguration);
        }
    }
}

TEST(SpIOpen_Message_FrameBroker, InitializeAllocatesStackWhenConfigurableAndMissingBuffer) {
    if constexpr (MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT) {
        BrokerHarness h;
        h.config.thread_stack_storage = etl::span<uint8_t>();
        ASSERT_TRUE(h.broker.Configure(h.config));
        ASSERT_TRUE(h.broker.Initialize());
        ASSERT_TRUE(h.broker.Start());
        ASSERT_TRUE(h.broker.Stop());
        ASSERT_TRUE(h.broker.Deinitialize());
    }
}

TEST(SpIOpen_Message_FrameBroker, ConfigureRejectsWrongState) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    auto ret = h.broker.Configure(h.config);
    EXPECT_FALSE(ret) << "Configure should fail from Inactive state";
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::InvalidState);
    }
}

TEST(SpIOpen_Message_FrameBroker, FullLifecycle) {
    BrokerHarness h;

    ASSERT_TRUE(h.ConfigureAll());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(h.broker.Initialize());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(h.broker.Start());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Active);

    ASSERT_TRUE(h.broker.Stop());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Inactive);

    ASSERT_TRUE(h.broker.Deinitialize());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Configured);

    ASSERT_TRUE(h.broker.Unconfigure());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Unconfigured);
}

TEST(SpIOpen_Message_FrameBroker, SubscribeAndUnsubscribe) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    SubscriberHarness sub("sub1", static_cast<uint16_t>(MessageType::MasterToSlave));
    ASSERT_TRUE(sub.InitializeMailbox());

    auto subscribe_ret = h.broker.Subscribe(&sub.handle);
    ASSERT_TRUE(subscribe_ret);

    auto unsubscribe_ret = h.broker.Unsubscribe(&sub.handle);
    ASSERT_TRUE(unsubscribe_ret);
}

TEST(SpIOpen_Message_FrameBroker, SubscribeRejectsWhenActive) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());
    ASSERT_TRUE(h.broker.Start());

    SubscriberHarness sub("sub1", static_cast<uint16_t>(MessageType::MasterToSlave));
    ASSERT_TRUE(sub.InitializeMailbox());

    auto ret = h.broker.Subscribe(&sub.handle);
    EXPECT_FALSE(ret) << "Subscribe should fail while broker is Active";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::InvalidState);
    }
}

TEST(SpIOpen_Message_FrameBroker, SubscribeRejectsNullHandle) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    auto ret = h.broker.Subscribe(nullptr);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::InvalidArgument);
    }
}

TEST(SpIOpen_Message_FrameBroker, SubscribeRejectsNullMailbox) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    subscriber::FrameSubscriberHandle_t bad_handle{"bad", nullptr, {0xFFFFU}, {0U}};
    auto ret = h.broker.Subscribe(&bad_handle);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::InvalidArgument);
    }
}

TEST(SpIOpen_Message_FrameBroker, SubscribeTableFull) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    std::array<SubscriberHarness*, FRAME_BROKER_MAX_SUBSCRIBERS> subs{};
    for (size_t i = 0U; i < FRAME_BROKER_MAX_SUBSCRIBERS; ++i) {
        subs[i] = new SubscriberHarness("sub", 0xFFFFU);
        ASSERT_TRUE(subs[i]->InitializeMailbox());
        auto ret = h.broker.Subscribe(&subs[i]->handle);
        ASSERT_TRUE(ret) << "Subscribe slot " << i << " should succeed";
    }

    SubscriberHarness overflow_sub("overflow", 0xFFFFU);
    ASSERT_TRUE(overflow_sub.InitializeMailbox());
    auto ret = h.broker.Subscribe(&overflow_sub.handle);
    EXPECT_FALSE(ret) << "Subscribe should fail when table is full";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::SubscriptionTableFull);
    }

    for (auto* s : subs) {
        delete s;
    }
}

TEST(SpIOpen_Message_FrameBroker, UnsubscribeNotFound) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    SubscriberHarness sub("ghost", 0xFFFFU);
    ASSERT_TRUE(sub.InitializeMailbox());

    auto ret = h.broker.Unsubscribe(&sub.handle);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::SubscriptionNotFound);
    }
}

TEST(SpIOpen_Message_FrameBroker, PublishRequiresActiveState) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Inactive);

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage{};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));

    auto ret = h.broker.Publish(&message, 0U);
    EXPECT_FALSE(ret) << "Publish should fail when broker is Inactive";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::InvalidState);
    }
}

TEST(SpIOpen_Message_FrameBroker, PublishRejectsNullMessage) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());
    ASSERT_TRUE(h.broker.Start());

    auto ret = h.broker.Publish(nullptr, 0U);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::InvalidArgument);
    }
}

TEST(SpIOpen_Message_FrameBroker, PublishEnqueuesToInbox) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());
    ASSERT_TRUE(h.broker.Start());

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage{};
    FrameMessage message(pool, etl::span<uint8_t>(frame_storage.data(), frame_storage.size()));
    ASSERT_TRUE(message.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
    EXPECT_EQ(message.GetReferenceCount(), 1U);

    auto ret = h.broker.Publish(&message, 0U);
    ASSERT_TRUE(ret) << "Publish should succeed when broker is Active";
    EXPECT_EQ(message.GetReferenceCount(), 2U) << "Publish should acquire inbox reference";
}

TEST(SpIOpen_Message_FrameBroker, UnconfigureClearsSubscribers) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    SubscriberHarness sub("sub1", static_cast<uint16_t>(MessageType::MasterToSlave));
    ASSERT_TRUE(sub.InitializeMailbox());
    ASSERT_TRUE(h.broker.Subscribe(&sub.handle));

    ASSERT_TRUE(h.broker.Start());
    ASSERT_TRUE(h.broker.Stop());
    ASSERT_TRUE(h.broker.Deinitialize());
    ASSERT_TRUE(h.broker.Unconfigure());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Unconfigured);

    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());

    auto ret = h.broker.Unsubscribe(&sub.handle);
    EXPECT_FALSE(ret) << "After Unconfigure, old subscriber should not be found";
    if (!ret) {
        EXPECT_EQ(ret.error(), FrameBrokerError::SubscriptionNotFound);
    }
}

TEST(SpIOpen_Message_FrameBroker, StopDrainsInbox) {
    BrokerHarness h;
    ASSERT_TRUE(h.ConfigureAll());
    ASSERT_TRUE(h.broker.Initialize());
    ASSERT_TRUE(h.broker.Start());

    FakeFramePool pool;
    std::array<uint8_t, 64U> frame_storage1{};
    std::array<uint8_t, 64U> frame_storage2{};
    FrameMessage msg1(pool, etl::span<uint8_t>(frame_storage1.data(), frame_storage1.size()));
    FrameMessage msg2(pool, etl::span<uint8_t>(frame_storage2.data(), frame_storage2.size()));
    ASSERT_TRUE(msg1.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));
    ASSERT_TRUE(msg2.AllocateToPublisher(MessageType::MasterToSlave, nullptr, 1U));

    ASSERT_TRUE(h.broker.Publish(&msg1, 0U));
    ASSERT_TRUE(h.broker.Publish(&msg2, 0U));
    EXPECT_EQ(msg1.GetReferenceCount(), 2U);
    EXPECT_EQ(msg2.GetReferenceCount(), 2U);

    ASSERT_TRUE(h.broker.Stop());
    EXPECT_EQ(h.broker.GetState(), LifecycleState::Inactive);
    EXPECT_EQ(msg1.GetReferenceCount(), 1U) << "Stop should release broker inbox reference";
    EXPECT_EQ(msg2.GetReferenceCount(), 1U) << "Stop should release broker inbox reference";
}
