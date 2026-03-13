/*
SpIOpen Broker Frame Allocator : Aggregates CC/FD/XL frame pools behind one API.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_broker_frame_pool.h"

namespace spiopen::broker {

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
     * @param fd_pool CAN-FD frame pool reference (present even when FD is disabled)
     * @param xl_pool CAN-XL frame pool reference (present even when XL is disabled)
     */
    FrameMessageAllocator(FramePool& cc_pool, FramePool& fd_pool, FramePool& xl_pool);

    ~FrameMessageAllocator() = default;

    /**
     * @brief Configures all underlying pools.
     *
     * If `SPIOPEN_BROKER_FRAME_POOL_SIZE_CONFIGURABLE` is enabled, per-pool message counts
     * are validated to be within KConfig maximums for their CAN message type.
     *
     * If `SPIOPEN_BROKER_FRAME_POOL_SIZE_CONFIGURABLE` is disabled, per-pool message counts
     * must exactly match compile-time KConfig maximums.
     *
     * This function also validates that `frame_buffer_size` for each pool meets the minimum
     * frame-size requirement of its target CAN message type (CC, FD, XL).
     *
     * @param cc_config CAN-CC pool config
     * @param fd_config CAN-FD pool config
     * @param xl_config CAN-XL pool config
     * @return Success when all configs are accepted; error if any pool rejects configuration
     */
    etl::expected<void, LifecycleError> Configure(const FramePoolConfig& cc_config, const FramePoolConfig& fd_config,
                                                  const FramePoolConfig& xl_config);

    /**
     * @brief Initializes all underlying pools.
     * @return Success on full initialization; error if any pool fails
     */
    etl::expected<void, LifecycleError> Initialize();

    /**
     * @brief Activates all underlying pools for allocation.
     * @return Success when all pools enter Active state; error if any pool fails
     */
    etl::expected<void, LifecycleError> Start();

    /**
     * @brief Deactivates allocation on all underlying pools while preserving requeue behavior.
     * @return Success when all pools enter Inactive state; error if any pool fails
     */
    etl::expected<void, LifecycleError> Stop();

    /**
     * @brief Deinitializes all underlying pools.
     * @return Success on full deinitialization; error if any pool fails
     */
    etl::expected<void, LifecycleError> Deinitialize();

    /**
     * @brief Gets combined lifecycle state across all pools.
     * @return Aggregated state (`LifecycleState::Mixed` when pools are inconsistent)
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
     * @brief Allocates a frame message from the matching pool.
     * @param can_message_type CAN message type to allocate from (CC/FD/XL)
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on unsupported size or pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateFrameMessage(
        format::CanMessageType can_message_type, message::MessageType message_type,
        const publisher::FramePublisherHandle_t* publisher_handle = nullptr, uint32_t timeout_ticks = 0U);

   private:
    /**
     * @brief Validates and normalizes one pool config for allocator-managed frame type.
     * @param config Input config for one backing `FramePool`
     * @param frame_type CAN frame type handled by that pool (CC/FD/XL)
     * @param default_name Default queue name to apply when `config.name` is nullptr
     * @return Normalized config (e.g., defaulted name) or lifecycle configuration error
     */
    etl::expected<FramePoolConfig, LifecycleError> ValidateAndNormalizePoolConfig(const FramePoolConfig& config,
                                                                                  format::CanMessageType frame_type,
                                                                                  const char* default_name) const;

    LifecycleState CombinePoolStates(LifecycleState cc_state, LifecycleState fd_state, LifecycleState xl_state) const;

    /**
     * @brief Selects backing pool for requested payload length.
     * @param required_payload_bytes Minimum payload bytes requested
     * @return Pointer to selected `FramePool`, or `nullptr` if unsupported
     */
    FramePool* SelectPoolForPayloadSize(size_t required_payload_bytes);

    /**
     * @brief Selects backing pool for requested CAN message type.
     * @param can_message_type CAN message type to allocate from (CC/FD/XL)
     * @return Pointer to selected `FramePool`, or `nullptr` if unsupported
     */
    FramePool* SelectPoolForCanMessageType(format::CanMessageType can_message_type);

    FramePool& cc_pool_;
    FramePool& fd_pool_;
    FramePool& xl_pool_;
};

}  // namespace spiopen::broker
