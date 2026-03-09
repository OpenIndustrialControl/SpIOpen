/*
SpIOpen Broker Frame Mailbox : Thread-safe message queue wrapper used by broker and subscribers.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "cmsis_os2.h"
#include "etl/expected.h"
#include "etl/span.h"
#include "spiopen_broker_frame_message.h"
#include "spiopen_broker_lifecycle.h"

namespace spiopen::broker {

/**
 * @brief Error codes returned by FrameMailbox operations.
 */
enum class FrameMailboxError : uint8_t {
    InvalidArgument = 1, /**< Invalid argument passed to mailbox API */
    NotInitialized,      /**< Mailbox queue not initialized */
    QueueFailure,        /**< RTOS queue operation failed */
    QueueTimeout,        /**< Timed queue operation expired */
    ResourceFailure,     /**< RTOS resource creation/deletion failed */
    DrainFailure,        /**< Mailbox drain could not dequeue/release all messages */
};

/**
 * @brief Configuration for mailbox queue creation.
 *
 * The mailbox always creates the queue internally. queue_storage is the optional
 * backing memory for that queue: if non-empty, the implementation uses it; if empty,
 * the mailbox allocates the backing memory internally.
 */
struct FrameMailboxConfig {
    size_t depth;                     /**< Number of message pointer slots in queue */
    etl::span<uint8_t> queue_storage; /**< Optional backing memory for the queue; empty = allocate internally */
};

/**
 * @brief Thread-safe mailbox wrapper for frame message pointer queues.
 *
 * The mailbox wraps RTOS queue operations used by broker and subscribers to exchange
 * message pointers while preserving reference-counted ownership semantics.
 */
class FrameMailbox : public ILifecycleComponent<FrameMailboxConfig, FrameMailboxError> {
   public:
    /**
     * @brief Constructs an uninitialized mailbox.
     */
    FrameMailbox();

    ~FrameMailbox() = default;

    /**
     * @brief Configures mailbox (depth and optional backing memory) before initialization.
     * @param config Mailbox configuration; queue is always created internally
     * @return Success on valid configuration, error on invalid args/state
     */
    etl::expected<void, FrameMailboxError> Configure(const FrameMailboxConfig& config) override;

    /**
     * @brief Initializes mailbox queue resources.
     *
     * Initializes resources and transitions to Inactive state. Operations are
     * not enabled until Start() transitions to Active.
     *
     * @return Success on initialization, error on invalid state/resource failure
     */
    etl::expected<void, FrameMailboxError> Initialize() override;

    /**
     * @brief Activates mailbox enqueue/dequeue operations.
     * @return Success on transition to Active; error on invalid state
     */
    etl::expected<void, FrameMailboxError> Start() override;

    /**
     * @brief Deactivates mailbox enqueue/dequeue operations.
     * @return Success on transition to Inactive; error on invalid state
     */
    etl::expected<void, FrameMailboxError> Stop() override;

    /**
     * @brief Deinitializes mailbox queue resources.
     *
     * Implementations are expected to drain and release all queued messages before
     * deleting the underlying RTOS queue resource.
     *
     * @return Success on deletion, error if queue is invalid, drain fails, or RTOS deletion fails
     */
    etl::expected<void, FrameMailboxError> Deinitialize() override;

    /**
     * @brief Drains all currently queued messages and releases each one.
     *
     * This helper is intended for shutdown paths and is expected to be called by
     * Deinitialize(). It repeatedly dequeues without blocking until queue is empty,
     * then calls Release() for each message.
     *
     * @return Number of drained messages on success, or mailbox error on failure
     */
    etl::expected<size_t, FrameMailboxError> DrainAndReleaseAll();

    /**
     * @brief Enqueues read-only frame message view pointer into mailbox after icnreasing reference count.
     * @param message Read-only frame message interface pointer to enqueue
     * @param timeout_ticks RTOS ticks to wait when queue is full (0 for non-blocking)
     * @return Success on enqueue; error on timeout/failure
     */
    etl::expected<void, FrameMailboxError> Enqueue(IReadOnlyFrameMessage* message, uint32_t timeout_ticks = 0U);

    /**
     * @brief Enqueues mutable frame message pointer into mailbox after increasing reference count.
     * @param message Mutable frame message pointer to enqueue
     * @param timeout_ticks RTOS ticks to wait when queue is full (0 for non-blocking)
     * @return Success on enqueue; error on timeout/failure
     */
    etl::expected<void, FrameMailboxError> Enqueue(FrameMessage* message, uint32_t timeout_ticks = 0U);

    /**
     * @brief Dequeues next read-only frame message view pointer from mailbox without decrementing reference count.
     * @param timeout_ticks RTOS ticks to wait when queue is empty
     * @return Message pointer on success, error on timeout/failure
     */
    etl::expected<IReadOnlyFrameMessage*, FrameMailboxError> Dequeue(uint32_t timeout_ticks = osWaitForever);

    /**
     * @brief Gets raw RTOS queue handle.
     * @return Underlying RTOS message queue handle
     */
    osMessageQueueId_t GetNativeHandle() const;

    /**
     * @brief Gets current mailbox lifecycle state.
     * @return Current LifecycleState
     */
    LifecycleState GetState() const override;

   private:
    // #TODO: Clarify whether mailbox enqueue/dequeue should automatically Acquire/Release references,
    // or if all reference management is exclusively handled by broker/publisher/subscriber call sites.
    // #TODO: For impossible Release() faults during drain, add centralized fault logging hook before hard abort.
    std::atomic<LifecycleState> state_;
    FrameMailboxConfig config_;
    bool using_external_storage_; /**< True when config_.queue_storage was non-empty at Configure */
    osMessageQueueId_t queue_handle_;
};

}  // namespace spiopen::broker
