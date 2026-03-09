/*
SpIOpen Broker Frame Broker : Used to link together multiple SpIOpen frame publishers and subscribers within one device.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "cmsis_os2.h"
#include "etl/expected.h"
#include "spiopen_broker_frame_mailbox.h"
#include "spiopen_broker_frame_message.h"
#include "spiopen_broker_frame_subscriber.h"
#include "spiopen_broker_lifecycle.h"

namespace spiopen::broker {

/**
 * @brief Error codes returned by frame broker APIs.
 */
enum class FrameBrokerError : uint8_t {
    InvalidArgument = 1,      /**< Invalid argument passed to API */
    InvalidState,             /**< API called in invalid broker state */
    ResourceFailure,          /**< RTOS resource creation or setup failed */
    QueueFailure,             /**< RTOS queue operation failed */
    SubscriptionTableFull,    /**< No free subscriber slots remain */
    SubscriptionNotFound,     /**< Subscriber was not registered */
    PublishFailed,            /**< Publish to inbound mailbox failed */
    NotInitialized,           /**< Broker resources not initialized */
};

/**
 * @brief Configuration for broker thread and inbound mailbox.
 *
 * The broker always creates and owns its inbox mailbox internally; inbox_mailbox_config
 * specifies depth and optional backing memory (static vs dynamic allocation) for that mailbox.
 */
struct FrameBrokerConfig {
    const char* thread_name;                  /**< Broker thread name, used by RTOS for diagnostics */
    uint32_t thread_priority;                  /**< RTOS thread priority for broker routing task */
    uint32_t thread_stack_size_bytes;         /**< Broker thread stack size in bytes */
    FrameMailboxConfig inbox_mailbox_config;   /**< Config for the internally owned inbox mailbox */
};

#ifdef CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT
static constexpr size_t FRAME_BROKER_MAX_SUBSCRIBERS = CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT;
#else
// #TODO: Confirm fallback subscriber table size when KConfig is unavailable.
static constexpr size_t FRAME_BROKER_MAX_SUBSCRIBERS = 8U;
#endif

/**
 * @brief Central broker that fans out published messages to all matching subscribers.
 *
 * The broker owns one inbound mailbox used by publishers. An internal thread blocks
 * on inbound messages and distributes them to subscriber mailboxes according to each
 * subscriber's message filter.
 */
class FrameBroker : public ILifecycleComponent<FrameBrokerConfig, FrameBrokerError> {
   public:
    /**
     * @brief Constructs an unconfigured broker.
     */
    FrameBroker();

    ~FrameBroker() = default;

    /**
     * @brief Configures broker runtime settings before initialization.
     * @param config Thread/mailbox configuration
     * @return Success when config is accepted; error on invalid args/state
     */
    etl::expected<void, FrameBrokerError> Configure(const FrameBrokerConfig& config) override;

    /**
     * @brief Initializes broker inbox and thread resources.
     *
     * On success, broker transitions to Inactive state and may then be started.
     *
     * @return Success on initialization, error on invalid state/resource failure
     */
    etl::expected<void, FrameBrokerError> Initialize() override;

    /**
     * @brief Starts the broker routing thread.
     * @return Success on start, error on invalid state/resource failure
     */
    etl::expected<void, FrameBrokerError> Start() override;

    /**
     * @brief Stops broker routing thread.
     * @return Success on stop, error on invalid state/resource failure
     */
    etl::expected<void, FrameBrokerError> Stop() override;

    /**
     * @brief Deinitializes broker resources and returns to Configured state.
     * @return Success on deinitialization; error on invalid state/resource failure
     */
    etl::expected<void, FrameBrokerError> Deinitialize() override;

    /**
     * @brief Resets broker subscriptions and internal counters while inactive.
     * @return Success on reset, error on invalid state
     */
    etl::expected<void, FrameBrokerError> Reset();

    /**
     * @brief Registers a subscriber for broker fan-out.
     * @param subscriber_handle Subscriber descriptor containing mailbox/filter metadata
     * @return Success if registered, error on invalid state/full table/invalid args
     */
    etl::expected<void, FrameBrokerError> Subscribe(subscriber::FrameSubscriberHandle_t* subscriber_handle);

    /**
     * @brief Removes a subscriber from broker fan-out.
     * @param subscriber_handle Previously registered subscriber descriptor
     * @return Success if removed, error on invalid state/not found/invalid args
     */
    etl::expected<void, FrameBrokerError> Unsubscribe(const subscriber::FrameSubscriberHandle_t* subscriber_handle);

    /**
     * @brief Publishes a frame message into broker inbound mailbox.
     * @param message Message allocated by FramePool and initialized by publisher
     * @param timeout_ticks RTOS ticks to wait for mailbox enqueue (0 for non-blocking)
     * @return Success on enqueue, error on invalid state/queue failure
     */
    etl::expected<void, FrameBrokerError> Publish(FrameMessage* message, uint32_t timeout_ticks = 0U);

    /**
     * @brief Gets current broker runtime state.
     * @return Current LifecycleState
     */
    LifecycleState GetState() const override;

    /**
     * @brief Gets global count of enqueue failures across all subscribers.
     * @return Atomic enqueue failure counter snapshot
     */
    uint32_t GetEnqueueErrorCount() const;

   private:
    static void ThreadEntry(void* context);
    void RunLoop();

    // #TODO: Confirm exact ownership semantics for Publish/FanOut:
    // whether broker acquires a reference on successful inbox enqueue or assumes caller already transferred ownership.
    etl::expected<void, FrameBrokerError> FanOutToSubscribers(FrameMessage* message);

    FrameBrokerConfig config_;
    std::atomic<LifecycleState> state_;
    osThreadId_t thread_id_;
    FrameMailbox inbox_mailbox_;

    std::array<subscriber::FrameSubscriberHandle_t*, FRAME_BROKER_MAX_SUBSCRIBERS> subscribers_;
    std::atomic<uint32_t> enqueue_error_count_;
};

}  // namespace spiopen::broker
