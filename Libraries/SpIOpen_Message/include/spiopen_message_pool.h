/*
SpIOpen Message Frame Pool : Used to manage a pool of SpIOpen frames that is shared among multiple publishers and
subscribers in a SpIOpen device.

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
#include "spiopen_frame_format.h"
#include "spiopen_lifecycle.h"
#include "spiopen_message.h"
#include "spiopen_message_publisher.h"

namespace spiopen::message {

using namespace spiopen::lifecycle;

/**
 * @brief Error codes for frame pool and allocator operations.
 */
enum class FramePoolError : uint8_t {
    InvalidArgument = 1,  /**< Invalid argument passed to API */
    InvalidState,         /**< API called in invalid lifecycle state */
    UnsupportedFrameSize, /**< Frame size class not supported by this build */
    PoolExhausted,        /**< No message available in pool within timeout */
    QueueFailure,         /**< RTOS queue operation failed */
    ResourceFailure,      /**< RTOS resource creation or setup failed */
};

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
static constexpr bool MESSAGE_CAN_XL_ENABLED = true;
#else
static constexpr bool MESSAGE_CAN_XL_ENABLED = false;
#endif

#ifdef CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_CC_FRAMES
static constexpr size_t MESSAGE_FRAME_POOL_MAX_CC_FRAMES = CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_CC_FRAMES;
#else
static constexpr size_t MESSAGE_FRAME_POOL_MAX_CC_FRAMES = 32U;
#endif

#ifdef CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_FD_FRAMES
static constexpr size_t MESSAGE_FRAME_POOL_MAX_FD_FRAMES = CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_FD_FRAMES;
#else
static constexpr size_t MESSAGE_FRAME_POOL_MAX_FD_FRAMES = 32U;
#endif

#ifdef CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_XL_FRAMES
static constexpr size_t MESSAGE_FRAME_POOL_MAX_XL_FRAMES = CONFIG_SPIOPEN_MESSAGE_FRAME_POOL_MAX_XL_FRAMES;
#else
static constexpr size_t MESSAGE_FRAME_POOL_MAX_XL_FRAMES = 32U;
#endif

static constexpr size_t MESSAGE_FRAME_POOL_MAX_FRAMES_BY_CAN_MESSAGE_TYPE[] = {
    MESSAGE_FRAME_POOL_MAX_CC_FRAMES, MESSAGE_FRAME_POOL_MAX_FD_FRAMES, MESSAGE_FRAME_POOL_MAX_XL_FRAMES};

/**
 * @brief Runtime configuration for one FramePool instance.
 *
 * The pool maintains an internal queue of available messages. pool_storage is the
 * optional backing memory for that queue (and any pool-internal layout): if non-empty,
 * the implementation uses it; if empty, Initialize() allocates backing memory only
 * when MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT is enabled.
 */
struct FramePoolConfig {
    size_t message_count;            /**< Number of FrameMessage objects in this pool. Zero is valid. */
    size_t frame_buffer_size;        /**< Byte size of each message's internal frame buffer */
    etl::span<uint8_t> pool_storage; /**< Optional backing memory for the pool; empty = allocate internally */
    const char* name = nullptr;      /**< Optional RTOS queue name used for diagnostics/debug visibility */
};

/**
 * @brief Gets required backing memory bytes for one FramePool instance.
 *
 * This is the one public sizing helper intended for both compile-time and runtime
 * buffer planning/validation.
 *
 * Internal layout follows:
 * 1) queue pointer storage
 * 2) FrameMessage object storage (aligned to alignof(FrameMessage))
 * 3) frame buffer storage sized by can_message_type max frame size
 *
 * @param message_count Number of messages in the pool
 * @param can_message_type CAN message/frame class (CC/FD/XL)
 * @return Total required bytes for pool backing storage
 */
static constexpr size_t BytesToAllocateForFramePool(size_t message_count, format::CanMessageType can_message_type) {
    const size_t queue_storage_size = message_count * sizeof(FrameMessage*);
    const size_t alignment = alignof(FrameMessage);
    const size_t messages_offset =
        (alignment == 0U) ? queue_storage_size : ((queue_storage_size + alignment - 1U) / alignment) * alignment;
    const size_t messages_storage_size = message_count * sizeof(FrameMessage);
    const size_t frame_buffers_storage_size =
        message_count * format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[static_cast<size_t>(can_message_type)];
    return messages_offset + messages_storage_size + frame_buffers_storage_size;
}

/**
 * @brief Memory pool of fixed-size FrameMessage objects for one frame length class.
 *
 * A FramePool owns message memory and recycles messages through an RTOS queue of available
 * slots. FrameMessage objects allocated from this pool point back to this pool to support
 * automatic return on final Release().
 *
 * Queue sizing invariant:
 * - available queue capacity is exactly message_count
 * - queue starts full at Initialize()
 * - each allocation dequeues one message pointer
 * - final message Release enqueues one pointer back
 *
 * Under this invariant, requeue on final Release should never fail in normal operation.
 */
class FramePool : public ILifecycleComponent<FramePoolConfig, LifecycleError> {
   public:
    /**
     * @brief Constructs an unconfigured frame pool.
     */
    FramePool();

    virtual ~FramePool() = default;

    /**
     * @brief Configures pool memory and count before initialization.
     Performs checks for memory sizing consistancy when external memory is used.
     * @param config Pool configuration (dynamic or static/external memory mode)
     * @return Success on valid config; error on invalid args/state
     */
    etl::expected<void, LifecycleError> Configure(const FramePoolConfig& config) override;

    /**
     * @brief Validates and normalizes frame pool configuration.
     * @param config Proposed pool config
     * @return Normalized config on success, InvalidConfiguration on validation failure
     */
    etl::expected<ConfigType, ErrorType> ValidateAndNormalizeConfiguration(const ConfigType& config) override;

    /**
     * @brief Initializes RTOS resources and prepares all messages for allocation.
     *
     * Initializes queue/resources and transitions to Inactive state. Fills the pool queue with all avalable messages.
     * Allocation is not enabled until Start() transitions the pool to Active.
     *
     * @return Success when pool is initialized; error on invalid state/resource failure
     */
    etl::expected<void, LifecycleError> Initialize() override;

    /**
     * @brief Activates pool for allocation operations.
     * @return Success on transition to Active; error on invalid state
     */
    etl::expected<void, LifecycleError> Start() override;

    /**
     * @brief Deactivates pool allocation while continuing to accept requeues.
     * @return Success on transition to Inactive; error on invalid state
     */
    etl::expected<void, LifecycleError> Stop() override;

    /**
     * @brief Deinitializes RTOS resources and resets pool state to configured.
     All messages must be returned to the pool before deinitialization so that their memory can be potentially freed.
     * @return Success when deinitialized; error on invalid state/resource failure
     */
    etl::expected<void, LifecycleError> Deinitialize() override;

    /**
     * @brief Clears pool configuration and transitions to Unconfigured.
     * @return Success on unconfigure; error on invalid lifecycle state
     */
    etl::expected<void, LifecycleError> Unconfigure() override;

    /**
     * @brief Allocates an available FrameMessage from this pool.
     *
     * Allocation is allowed only in Active state. Returns a FrameMessage in Allocated state with a single reference,
     * owned by the caller of this function. O r PoolExhausted error is returned if no messages are available within the
     * timeout. ISR-safe when timeout_ticks is 0.
     *
     * @param message_type Message type metadata to attach for publish
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for a free message (0 for non-blocking)
     * @return Pointer to ready-to-fill FrameMessage on success, error on timeout/failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateFrameMessage(
        MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint32_t timeout_ticks = 0U);

    /**
     * @brief Requeues a message back to this pool after final Release().
     * @param message Message pointer previously allocated from this pool
     *
     * This function is intended for final release path and is treated as infallible
     * under the queue sizing invariant. If requeue fails due to an impossible edge
     * case (e.g. invalid/uninitialized pool), implementation should hard-fault/abort.
     *
     * Virtual to allow test doubles (e.g. FakeFramePool) to override and record calls.
     *
     * #TODO: Add centralized fault logging hook before hard abort.
     */
    virtual void RequeueFrameMessage(FrameMessage* message);

    /**
     * @brief Gets current lifecycle state.
     * @return Current LifecycleState
     */
    LifecycleState GetState() const override;

    /**
     * @brief Gets configured frame buffer byte length of this pool.
     * @return Frame buffer size in bytes
     */
    size_t GetFrameBufferSize() const;

    /**
     * @brief Gets configured message capacity of this pool.
     * @return Total message count
     */
    size_t GetMessageCount() const;

   private:
    etl::span<uint8_t> active_storage_; /**< Active backing storage used by the current initialized pool layout */
    std::atomic<LifecycleState> state_; /**< Primary lifecycle state for configure/init/start/stop/deinit/unconfigure */
    FramePoolConfig config_;            /**< Last accepted configuration for this pool instance */
    osMessageQueueId_t
        available_messages_queue_; /**< Queue of available FrameMessage* entries (nullptr when not initialized) */
    uint8_t*
        owned_storage_; /**< Heap allocation owned by the pool when internal storage mode is used; nullptr otherwise */
};

}  // namespace spiopen::message
