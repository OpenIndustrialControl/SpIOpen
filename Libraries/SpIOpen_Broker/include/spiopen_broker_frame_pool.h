/*
SpIOpen Broker Frame Pool : Used to manage a pool of SpIOpen frames that is shared among multiple publishers and
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
#include "spiopen_broker_frame_message.h"
#include "spiopen_broker_frame_publisher.h"
#include "spiopen_broker_lifecycle.h"
#include "spiopen_frame_format.h"

namespace spiopen::broker {

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
    NotInitialized,       /**< Pool accessed before initialization */
    NotActive,            /**< Operation requires active pool state */
};

/**
 * @brief Runtime configuration for one FramePool instance.
 *
 * The pool maintains an internal queue of available messages. pool_storage is the
 * optional backing memory for that queue (and any pool-internal layout): if non-empty,
 * the implementation uses it; if empty, the pool allocates backing memory internally.
 */
struct FramePoolConfig {
    size_t message_count;            /**< Number of FrameMessage objects in this pool */
    size_t frame_buffer_size;        /**< Byte size of each message's internal frame buffer */
    etl::span<uint8_t> pool_storage; /**< Optional backing memory for the pool; empty = allocate internally */
};

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
class FramePool : public ILifecycleComponent<FramePoolConfig, FramePoolError> {
   public:
    /**
     * @brief Constructs an unconfigured frame pool.
     */
    FramePool();

    ~FramePool() = default;

    /**
     * @brief Configures pool memory and count before initialization.
     * @param config Pool configuration (dynamic or static/external memory mode)
     * @return Success on valid config; error on invalid args/state
     */
    etl::expected<void, FramePoolError> Configure(const FramePoolConfig& config) override;

    /**
     * @brief Initializes RTOS resources and prepares all messages for allocation.
     *
     * Initializes queue/resources and transitions to Inactive state. Allocation
     * is not enabled until Start() transitions the pool to Active.
     *
     * @return Success when pool is initialized; error on invalid state/resource failure
     */
    etl::expected<void, FramePoolError> Initialize() override;

    /**
     * @brief Activates pool for allocation operations.
     * @return Success on transition to Active; error on invalid state
     */
    etl::expected<void, FramePoolError> Start() override;

    /**
     * @brief Deactivates pool allocation while continuing to accept requeues.
     * @return Success on transition to Inactive; error on invalid state
     */
    etl::expected<void, FramePoolError> Stop() override;

    /**
     * @brief Deinitializes RTOS resources and resets pool state to configured.
     * @return Success when deinitialized; error on invalid state/resource failure
     */
    etl::expected<void, FramePoolError> Deinitialize() override;

    /**
     * @brief Allocates an available FrameMessage from this pool.
     *
     * Allocation is allowed only in Active state.
     *
     * @param message_type Message type metadata to attach for publish
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for a free message (0 for non-blocking)
     * @return Pointer to ready-to-fill FrameMessage on success, error on timeout/failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateFrameMessage(
        message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint32_t timeout_ticks = 0U);

    /**
     * @brief Requeues a message back to this pool after final Release().
     * @param message Message pointer previously allocated from this pool
     *
     * This function is intended for final release path and is treated as infallible
     * under the queue sizing invariant. If requeue fails due to an impossible edge
     * case (e.g. invalid/uninitialized pool), implementation should hard-fault/abort.
     *
     * #TODO: Add centralized fault logging hook before hard abort.
     */
    void RequeueFrameMessage(FrameMessage* message);

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
    std::atomic<LifecycleState> state_;
    FramePoolConfig config_;
    osMessageQueueId_t available_messages_queue_;
};

/**
 * @brief Multi-pool allocator that routes allocation requests to CC/FD/XL pools.
 *
 * This class provides a single allocation touch-point for publishers while preserving
 * size-specialized pools for deterministic memory use.
 */
class FrameMessageAllocator {
   public:
    /**
     * @brief Constructs allocator from externally owned frame pools.
     * @param cc_pool CAN-CC frame pool reference
     * @param fd_pool CAN-FD frame pool reference (only when CAN-FD is enabled)
     * @param xl_pool CAN-XL frame pool reference (only when CAN-XL is enabled)
     */
    FrameMessageAllocator(FramePool& cc_pool
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
                          ,
                          FramePool& fd_pool
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
                          ,
                          FramePool& xl_pool
#endif
    );

    ~FrameMessageAllocator() = default;

    /**
     * @brief Configures all underlying pools.
     * @param cc_config CAN-CC pool config
     * @param fd_config CAN-FD pool config (only when CAN-FD is enabled)
     * @param xl_config CAN-XL pool config (only when CAN-XL is enabled)
     * @return Success when all configs are accepted; error if any pool rejects configuration
     */
    etl::expected<void, FramePoolError> Configure(const FramePoolConfig& cc_config
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
                                                  ,
                                                  const FramePoolConfig& fd_config
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
                                                  ,
                                                  const FramePoolConfig& xl_config
#endif
    );

    /**
     * @brief Initializes all underlying pools.
     * @return Success on full initialization; error if any pool fails
     */
    etl::expected<void, FramePoolError> Initialize();

    /**
     * @brief Activates all underlying pools for allocation.
     * @return Success when all pools enter Active state; error if any pool fails
     */
    etl::expected<void, FramePoolError> Start();

    /**
     * @brief Deactivates allocation on all underlying pools while preserving requeue behavior.
     * @return Success when all pools enter Inactive state; error if any pool fails
     */
    etl::expected<void, FramePoolError> Stop();

    /**
     * @brief Deinitializes all underlying pools.
     * @return Success on full deinitialization; error if any pool fails
     */
    etl::expected<void, FramePoolError> Deinitialize();

    /**
     * @brief Gets combined lifecycle state across all enabled frame pools.
     * @return Aggregated state (LifecycleState::Mixed when pools are inconsistent)
     */
    LifecycleState GetState() const;

    /**
     * @brief Allocates a frame message from the smallest suitable pool.
     * @param required_payload_bytes Minimum payload bytes requested by publisher
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on unsupported size or pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateFrameMessage(
        size_t required_payload_bytes, message::MessageType message_type,
        const publisher::FramePublisherHandle_t* publisher_handle = nullptr, uint32_t timeout_ticks = 0U);

    /**
     * @brief Allocates a CAN-CC message from the CC pool.
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateCanCcFrameMessage(
        message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint32_t timeout_ticks = 0U);

#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    /**
     * @brief Allocates a CAN-FD message from the FD pool.
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateCanFdFrameMessage(
        message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint32_t timeout_ticks = 0U);
#endif

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    /**
     * @brief Allocates a CAN-XL message from the XL pool.
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateCanXlFrameMessage(
        message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint32_t timeout_ticks = 0U);
#endif

   private:
    LifecycleState CombinePoolStates(LifecycleState cc_state
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
                                     ,
                                     LifecycleState fd_state
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
                                     ,
                                     LifecycleState xl_state
#endif
    ) const;

    /**
     * @brief Selects backing pool for requested payload length.
     * @param required_payload_bytes Minimum payload bytes requested
     * @return Pointer to selected FramePool, or nullptr if unsupported
     */
    FramePool* SelectPoolForPayloadSize(size_t required_payload_bytes);

    FramePool& cc_pool_;
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
    FramePool& fd_pool_;
#endif
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    FramePool& xl_pool_;
#endif
};

}  // namespace spiopen::broker
