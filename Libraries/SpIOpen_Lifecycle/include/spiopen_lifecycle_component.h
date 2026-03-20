/*
SpIOpen Lifecycle Component Interface : Base lifecycle contract for configurable components.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include "etl/expected.h"
#include "spiopen_lifecycle_core.h"

namespace spiopen::lifecycle {

/**
 * @brief Pure-virtual interface for components following the common lifecycle contract.
 *
 * @tparam ConfigT Component-specific configuration structure
 * @tparam TError Component-specific error type
 */
template <typename ConfigT, typename TError>
class ILifecycleComponent {
   public:
    using ConfigType = ConfigT;
    using ErrorType = TError;

    virtual ~ILifecycleComponent() = default;

    /**
     * @brief Applies validated configuration and moves toward @ref LifecycleState::Configured.
     *
     * Contract: valid only from @ref LifecycleState::Unconfigured (first configure) or
     * @ref LifecycleState::Configured (reconfigure). Implementations typically enter
     * @ref LifecycleState::Configuring during work, then @ref LifecycleState::Configured on success.
     * Call @ref ValidateAndNormalizeConfiguration internally (or equivalent) so validation stays in one place.
     * Return @c LifecycleErrorType::InvalidState when the current state does not allow configuration;
     * return @c LifecycleErrorType::InvalidConfiguration (or a component-specific equivalent) when @p config is
     * rejected.
     *
     * @param config Component runtime configuration
     * @return Success when configuration is stored/accepted
     */
    virtual etl::expected<void, TError> Configure(const ConfigT& config) = 0;

    /**
     * @brief Validates and normalizes a proposed configuration without implying a full lifecycle transition by itself.
     *
     * Contract: this is the single source of truth for “is this config well-formed?” Implementations should return
     * @c LifecycleErrorType::InvalidConfiguration when @p config is invalid. It is normally invoked from
     * @ref Configure (or from aggregate parents coordinating children). It does not change lifecycle state unless an
     * implementation explicitly documents otherwise; callers must still follow @ref Configure / @ref Initialize order.
     *
     * @param config Proposed component configuration
     * @return Normalized configuration on success
     */
    virtual etl::expected<ConfigT, TError> ValidateAndNormalizeConfiguration(const ConfigT& config) = 0;

    /**
     * @brief Allocates or attaches runtime resources and transitions to @ref LifecycleState::Inactive on success.
     *
     * Contract: valid only from @ref LifecycleState::Configured (configuration present, not initialized). Enters
     * @ref LifecycleState::Initializing during work, then @ref LifecycleState::Inactive when ready for @ref Start.
     * Return @c LifecycleErrorType::InvalidState if already initialized, not configured, or otherwise not in
     * Configured. On failure, implementations should restore @ref LifecycleState::Configured if partial work can be
     * rolled back.
     *
     * @return Success on initialization
     */
    virtual etl::expected<void, TError> Initialize() = 0;

    /**
     * @brief Enables normal runtime operation and transitions to @ref LifecycleState::Active on success.
     *
     * Contract: valid only from @ref LifecycleState::Inactive. May pass through @ref LifecycleState::Starting.
     * Return @c LifecycleErrorType::InvalidState when not inactive (for example still @ref LifecycleState::Configured
     * or already @ref LifecycleState::Active). Call @ref Initialize before @ref Start when bringing a component up.
     *
     * @return Success on start
     */
    virtual etl::expected<void, TError> Start() = 0;

    /**
     * @brief Disables runtime operation and returns to @ref LifecycleState::Inactive on success.
     *
     * Contract: valid only from @ref LifecycleState::Active. May pass through @ref LifecycleState::Stopping.
     * Return @c LifecycleErrorType::InvalidState when not active. Use before @ref Deinitialize when tearing down.
     *
     * @return Success on stop
     */
    virtual etl::expected<void, TError> Stop() = 0;

    /**
     * @brief Releases runtime resources and transitions to @ref LifecycleState::Configured on success.
     *
     * Contract: valid only from @ref LifecycleState::Inactive (after @ref Stop). Enters
     * @ref LifecycleState::Deinitializing during work. Return @c LifecycleErrorType::InvalidState when not inactive.
     * After success, configuration remains valid so @ref Initialize may run again without a new @ref Configure.
     *
     * @return Success on deinitialization
     */
    virtual etl::expected<void, TError> Deinitialize() = 0;

    /**
     * @brief Clears stored configuration/runtime bindings and transitions to Unconfigured on success.
     *
     * Contract: this transition is only valid when the current state is @ref LifecycleState::Configured
     * (after @ref Deinitialize, or if configured but never initialized). It does **not** mean “restart the FSM
     * from any state”: call @ref Stop / @ref Deinitialize first as required. Implementations should return
     * @c LifecycleErrorType::InvalidState when invoked from any other state.
     *
     * @return Success on unconfigure
     */
    virtual etl::expected<void, TError> Unconfigure() = 0;

    /**
     * @brief Returns the current lifecycle state for this component.
     *
     * Contract: non-mutating snapshot of the last completed transition (or in-progress transitional state if the
     * implementation exposes it, such as @ref LifecycleState::Configuring). Thread-safety and atomicity are
     * implementation-defined; callers should not infer ordering with concurrent transitions without external
     * synchronization or documented guarantees.
     *
     * @return Current lifecycle state
     */
    virtual LifecycleState GetState() const = 0;
};

}  // namespace spiopen::lifecycle
