/*
SpIOpen Broker Lifecycle : Shared lifecycle state enum for pool, mailbox, broker, and allocator.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstdint>

#include "etl/expected.h"

namespace spiopen::broker {

/**
 * @brief Shared lifecycle state for FramePool, FrameMailbox, FrameBroker, and FrameMessageAllocator.
 *
 * All broker components use this enum so callers can treat state uniformly.
 * Mixed is used only by FrameMessageAllocator when underlying pools are in inconsistent states.
 */
enum class LifecycleState : uint8_t {
    Unconfigured = 0, /**< Not configured yet */
    Configuring,      /**< Configuration update in progress */
    Configured,       /**< Configuration valid, not initialized */
    Initializing,     /**< Initialization in progress */
    Inactive,         /**< Initialized; not active for normal operations */
    Starting,         /**< Activation in progress */
    Active,           /**< Active; normal operations allowed */
    Stopping,         /**< Deactivation in progress */
    Deinitializing,   /**< Resource teardown in progress */
    Mixed,            /**< (Allocator only) Underlying pools in non-uniform states */
    Faulted,          /**< Fatal error state; no further operations allowed */
    Recovering,       /**< Recovering from fault state */
};

/**
 * @brief Pure-virtual interface for broker components following the common lifecycle.
 *
 * Enforces the common lifecycle transition API while allowing each class to
 * define its own config and error enums.
 *
 * @tparam ConfigT Class-specific configuration structure
 * @tparam ErrorT Class-specific error enum
 */
template <typename ConfigT, typename ErrorT>
class ILifecycleComponent {
   public:
    using ConfigType = ConfigT;
    using ErrorType = ErrorT;

    virtual ~ILifecycleComponent() = default;

    /**
     * @brief Applies configuration in Unconfigured/Configured states.
     * @param config Class-specific runtime configuration
     * @return Success when configuration is accepted
     */
    virtual etl::expected<void, ErrorT> Configure(const ConfigT& config) = 0;

    /**
     * @brief Initializes resources and transitions to Inactive on success.
     * @return Success on initialization
     */
    virtual etl::expected<void, ErrorT> Initialize() = 0;

    /**
     * @brief Starts runtime behavior and transitions to Active on success.
     * @return Success on start
     */
    virtual etl::expected<void, ErrorT> Start() = 0;

    /**
     * @brief Stops runtime behavior and transitions to Inactive on success.
     * @return Success on stop
     */
    virtual etl::expected<void, ErrorT> Stop() = 0;

    /**
     * @brief Deinitializes resources and transitions to Configured on success.
     * @return Success on deinitialization
     */
    virtual etl::expected<void, ErrorT> Deinitialize() = 0;

    /**
     * @brief Returns the current shared lifecycle state.
     * @return Current lifecycle state
     */
    virtual LifecycleState GetState() const = 0;
};

}  // namespace spiopen::broker
