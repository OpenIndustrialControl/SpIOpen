/*
SpIOpen Message Frame Allocator : Aggregates CC/FD/XL frame pools behind one API.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "spiopen_message_pool.h"

namespace spiopen::message {

/**
 * @brief Multi-pool allocator that routes allocation requests to CC/FD/XL pools.
 *
 * This class provides a single allocation touch-point for publishers while preserving
 * size-specialized pools for deterministic memory use.
 *
 * Derives from IAggregateLifecycleComponent to delegate lifecycle transitions
 * (Configure, Initialize, Start, Stop, Deinitialize, Unconfigure) to the three child
 * FramePool instances, with automatic per-child error aggregation.
 */
class FrameMessageAllocator : public IAggregateLifecycleComponent<NoLocalConfig, FramePool, FramePool, FramePool> {
   public:
    using AggregateBase = IAggregateLifecycleComponent<NoLocalConfig, FramePool, FramePool, FramePool>;

    /**
     * @brief Constructs allocator from externally owned frame pools.
     * @param cc_pool CAN-CC frame pool reference
     * @param fd_pool CAN-FD frame pool reference (present even when FD is disabled)
     * @param xl_pool CAN-XL frame pool reference (present even when XL is disabled)
     */
    FrameMessageAllocator(FramePool& cc_pool, FramePool& fd_pool, FramePool& xl_pool);

    ~FrameMessageAllocator() override = default;

    /**
     * @brief Allocates a frame message from the smallest suitable pool.
     * @param required_payload_bytes Minimum payload bytes requested by publisher
     * @param message_type Message type metadata to assign to allocated message
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param timeout_ticks RTOS ticks to wait for allocation (0 for non-blocking)
     * @return FrameMessage pointer on success; error on unsupported size or pool failure
     */
    etl::expected<FrameMessage*, FramePoolError> AllocateFrameMessage(
        size_t required_payload_bytes, MessageType message_type,
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
        format::CanMessageType can_message_type, MessageType message_type,
        const publisher::FramePublisherHandle_t* publisher_handle = nullptr, uint32_t timeout_ticks = 0U);

   protected:
    /**
     * @brief Validates and normalizes child pool configurations before child Configure() fanout.
     *
     * Applies default RTOS queue names when config.name is nullptr, validates frame_buffer_size
     * meets the minimum for each CAN message type, enforces KConfig message count constraints,
     * and validates that disabled CAN types have zero message counts.
     *
     * @param child_config Tuple of (cc_config, fd_config, xl_config)
     * @return Normalized child config tuple on success, or aggregate error on validation failure
     */
    etl::expected<ChildConfigTuple, ErrorType> ValidateAndNormalizeChildrenConfigurations(
        const ChildConfigTuple& child_config) override;

   private:
    static constexpr size_t kCcPoolIndex = 0;
    static constexpr size_t kFdPoolIndex = 1;
    static constexpr size_t kXlPoolIndex = 2;

    /**
     * @brief Validates and normalizes one pool config for allocator-managed frame type.
     * @param config Input config for one backing FramePool
     * @param frame_type CAN frame type handled by that pool (CC/FD/XL)
     * @param default_name Default queue name to apply when config.name is nullptr
     * @return Normalized config (e.g., defaulted name) or lifecycle configuration error
     */
    static etl::expected<FramePoolConfig, LifecycleError> ValidateAndNormalizePoolConfig(
        const FramePoolConfig& config, format::CanMessageType frame_type, const char* default_name);

    /**
     * @brief Selects backing pool for requested payload length.
     * @param required_payload_bytes Minimum payload bytes requested
     * @return Pointer to selected FramePool, or nullptr if unsupported
     */
    FramePool* SelectPoolForPayloadSize(size_t required_payload_bytes);

    /**
     * @brief Selects backing pool for requested CAN message type.
     * @param can_message_type CAN message type to allocate from (CC/FD/XL)
     * @return Pointer to selected FramePool, or nullptr if unsupported
     */
    FramePool* SelectPoolForCanMessageType(format::CanMessageType can_message_type);
};

}  // namespace spiopen::message
