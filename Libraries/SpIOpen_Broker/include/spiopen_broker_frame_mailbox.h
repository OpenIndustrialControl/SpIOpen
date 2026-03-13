/*
SpIOpen Broker Frame Mailbox : Thread-safe message queue wrapper used by broker and subscribers.

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
#include "etl/span.h"
#include "spiopen_broker_frame_message.h"
#include "spiopen_broker_lifecycle.h"

namespace spiopen::broker {

#ifdef CONFIG_SPIOPEN_BROKER_MAILBOX_MAX_DEPTH
static constexpr size_t BROKER_MAILBOX_MAX_DEPTH = CONFIG_SPIOPEN_BROKER_MAILBOX_MAX_DEPTH;
#else
static constexpr size_t BROKER_MAILBOX_MAX_DEPTH = 32U;
#endif

#ifdef CONFIG_SPIOPEN_BROKER_MAILBOX_DEPTH_CONFIGURABLE
static constexpr bool BROKER_MAILBOX_DEPTH_CONFIGURABLE =
    (CONFIG_SPIOPEN_BROKER_MAILBOX_DEPTH_CONFIGURABLE != 0);
#else
// #NOTE: In host/unit-test builds where Kconfig values are not injected as compile definitions,
// default to runtime-configurable depth to keep tests deterministic and decoupled from build-system plumbing.
static constexpr bool BROKER_MAILBOX_DEPTH_CONFIGURABLE = true;
#endif

/**
 * @brief Error codes returned by FrameMailbox operations.
 *
 * #TODO: As lifecycle/RTOS behavior is finalized, revisit this enum and adjust
 * names/granularity to match final mailbox state/error mapping.
 */
enum class FrameMailboxError : uint8_t {
    InvalidArgument = 1, /**< Invalid argument passed to mailbox API */
    InvalidState,        /**< API called in invalid mailbox lifecycle state */
    NotInitialized,      /**< Mailbox queue resources are not initialized */
    NotActive,           /**< Operation requires mailbox to be Active */
    PreconditionFailed,  /**< Non-state precondition failed (e.g. message not enqueueable) */
    QueueFailure,        /**< RTOS queue operation failed */
    QueueTimeout,        /**< Timed queue operation expired */
    ResourceFailure,     /**< RTOS resource creation/deletion failed */
};

/**
 * @brief Configuration for mailbox queue creation.
 *
 * The mailbox always creates the queue internally. queue_storage is optional
 * backing memory for queue element storage (message pointer slots): if non-empty,
 * the implementation uses it; if empty, the mailbox allocates element storage
 * internally unless build configuration requires external storage. name is an
 * optional diagnostic queue name; if omitted, a library default is used.
 * SPIOPEN_BROKER_MAILBOX_DEPTH_CONFIGURABLE is used to determine if the mailbox depth is configurable at runtime.
 * SPIOPEN_BROKER_MAILBOX_MAX_DEPTH is the maximum depth of a run-time configurable mailbox,
 * or, if depth is not configurable, this is the specific fixed depth of the mailbox.
 * Queue control-block storage is always internal to each mailbox instance and
 * is not configurable at runtime.
 */
struct FrameMailboxConfig {
    size_t depth;                     /**< Number of message pointer slots in queue */
    etl::span<uint8_t> queue_storage; /**< Optional backing memory for the queue; empty = allocate internally */
    const char* name = nullptr;       /**< Optional queue name for diagnostics; nullptr or empty uses default */
};

/**
 * @brief Thread-safe mailbox wrapper for frame message pointer queues.
 *
 * The mailbox wraps RTOS queue operations used by broker and subscribers to exchange
 * message pointers while preserving reference-counted ownership semantics.
 */
class FrameMailbox : public ILifecycleComponent<FrameMailboxConfig, LifecycleError> {
   public:
    /**
     * @brief Constructs an uninitialized mailbox.
     */
    FrameMailbox();

    ~FrameMailbox() = default;

    /**
     * @brief Configures mailbox (depth and optional backing memory) before initialization.
     * @param config Mailbox configuration; queue is always created internally
     * @return Success, or LifecycleErrorType::InvalidArgument / LifecycleErrorType::InvalidState
     */
    etl::expected<void, LifecycleError> Configure(const FrameMailboxConfig& config) override;

    /**
     * @brief Initializes mailbox queue resources.
     *
     * Initializes resources and transitions to Inactive state. Operations are
     * not enabled until Start() transitions to Active.
     *
     * @return Success, or LifecycleErrorType::NotConfigured / LifecycleErrorType::InvalidState /
     * LifecycleErrorType::ResourceFailure
     */
    etl::expected<void, LifecycleError> Initialize() override;

    /**
     * @brief Activates mailbox enqueue/dequeue operations.
     * @return Success, or LifecycleErrorType::NotInitialized / LifecycleErrorType::InvalidState
     */
    etl::expected<void, LifecycleError> Start() override;

    /**
     * @brief Deactivates mailbox enqueue/dequeue operations.
     * @return Success, or LifecycleErrorType::InvalidState
     */
    etl::expected<void, LifecycleError> Stop() override;

    /**
     * @brief Deinitializes mailbox queue resources.
     *
     * Deinitialize requires the queue to be empty and then releases queue resources.
     * Callers are expected to Stop(), then drain messages manually (one-by-one via
     * Dequeue/Release or all-at-once via DrainAndReleaseAll()), and only then call
     * Deinitialize().
     * @note Not ISR-safe. Must not be called from ISR context.
     *
     * @return Success, or LifecycleErrorType::InvalidState / LifecycleErrorType::ResourceFailure /
     * LifecycleErrorType::NotAllowedInIsr
     */
    etl::expected<void, LifecycleError> Deinitialize() override;

    /**
     * @brief Clears mailbox configuration and transitions to Unconfigured.
     *
     * Valid only from Configured state (after Deinitialize()).
     *
     * @return Success on reset, or LifecycleErrorType::NotConfigured /
     * LifecycleErrorType::InvalidState
     */
    etl::expected<void, LifecycleError> Reset() override;

    /**
     * @brief Drains all currently queued messages and releases each one.
     *
     * This helper is intended for shutdown paths. It repeatedly dequeues without
     * blocking until the queue is empty, calling Release() for each message as it is dequeued.
     * Because Dequeue() is move-like, this helper performs the matching release. Even in the case of errors, draining
     * the queue to free up its resource is the priority and one error message will be returned at the end.
     *
     * @return Drained message count, or FrameMailboxError::NotInitialized / FrameMailboxError::InvalidState /
     * FrameMailboxError::QueueFailure / FrameMailboxError::ResourceFailure
     */
    etl::expected<size_t, FrameMailboxError> DrainAndReleaseAll();

    /**
     * @brief Enqueues mutable frame message pointer into mailbox.
     *
     * Enqueue is allowed only while mailbox is Active. It acquires one additional
     * message reference before queue submission, and if queue submission fails
     * (timeout/full/failure), it rolls back that reference via Release().
     *
     * @param message Mutable frame message pointer to enqueue
     * @param timeout_ticks RTOS ticks to wait when queue is full (0 for non-blocking)
     * @note ISR-safe when timeout_ticks is 0.
     * @return Success, or FrameMailboxError::InvalidArgument / FrameMailboxError::NotActive /
     * FrameMailboxError::NotInitialized / FrameMailboxError::PreconditionFailed /
     * FrameMailboxError::QueueTimeout / FrameMailboxError::QueueFailure / FrameMailboxError::ResourceFailure
     */
    etl::expected<void, FrameMailboxError> Enqueue(FrameMessage* message, uint32_t timeout_ticks = 0U);

    /**
     * @brief Dequeues next frame message pointer from mailbox without decrementing reference count.
     *
     * Dequeue is move-like ownership transfer of one queued reference. It is valid
     * while mailbox is Initialized (Inactive drain mode) or Active.
     * A future peek-style API could be added to return shared access semantics.
     *
     * @param timeout_ticks RTOS ticks to wait when queue is empty
     * @note ISR-safe when timeout_ticks is 0.
     * @return Message pointer, or FrameMailboxError::NotInitialized / FrameMailboxError::QueueTimeout /
     * FrameMailboxError::QueueFailure / FrameMailboxError::InvalidState / FrameMailboxError::ResourceFailure
     */
    etl::expected<FrameMessage*, FrameMailboxError> Dequeue(uint32_t timeout_ticks = osWaitForever);

    /**
     * @brief Gets current mailbox lifecycle state.
     * @return Current LifecycleState
     */
    LifecycleState GetState() const override;

   private:
    // Queue control block storage is always owned by the mailbox instance.
#ifdef osRtxMessageQueueCbSize
    static constexpr size_t kQueueControlBlockStorageSize = osRtxMessageQueueCbSize;
#else
    // #NOTE: Non-RTX/test builds may not expose osRtxMessageQueueCbSize; use a
    // conservative fallback while still keeping control-block storage internal.
    static constexpr size_t kQueueControlBlockStorageSize = 64U;
#endif

    // Inactive acts as drain-only mode: Enqueue is disabled, Dequeue/Drain are allowed.
    // #TODO: Add centralized fault logging hook for impossible drain/release fault paths.
    // Intentionally do not expose native queue handle to preserve mailbox lifecycle
    // and reference-counting invariants through Enqueue/Dequeue/Drain APIs.
    std::array<uint8_t, kQueueControlBlockStorageSize> queue_control_block_storage_;
    std::atomic<LifecycleState> state_;
    FrameMailboxConfig config_;
    osMessageQueueId_t queue_handle_;
};

}  // namespace spiopen::broker
