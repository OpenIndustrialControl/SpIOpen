/*
SpIOpen Lifecycle Core : Shared lifecycle states, error types, and aggregate helpers.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "etl/optional.h"
#include "etl/tuple.h"

namespace spiopen::lifecycle {

/**
 * @brief Shared lifecycle state for configurable/initializable runtime components.
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
    Unconfiguring,    /**< Clearing configuration in progress (Configured → Unconfigured) */
    Mixed,            /**< Aggregate component children are in non-uniform states */
};

/**
 * @brief Identifies a lifecycle operation / edge in the state machine.
 *
 * Values align with ILifecycleComponent virtuals that drive state changes.
 */
enum class LifecycleTransition : uint8_t {
    Configure = 0, /**< ILifecycleComponent::Configure */
    Initialize,    /**< ILifecycleComponent::Initialize */
    Start,         /**< ILifecycleComponent::Start */
    Stop,          /**< ILifecycleComponent::Stop */
    Deinitialize,  /**< ILifecycleComponent::Deinitialize */
    /**
     * ILifecycleComponent::Unconfigure — clears configuration and ends in Unconfigured.
     * Valid only from @ref LifecycleState::Configured (not a global “reset to initial” from arbitrary states).
     */
    Unconfigure, /**< ILifecycleComponent::Unconfigure */
};

/**
 * @brief Generic primary error codes for lifecycle transitions.
 */
enum class LifecycleErrorType : uint8_t {
    InvalidArgument = 1,    /**< Invalid argument passed to lifecycle transition */
    InvalidConfiguration,   /**< Configuration is invalid and must be rejected */
    InvalidState,           /**< Lifecycle transition called in invalid current state */
    AggregateError,         /**< Error originated from one or more child lifecycle components */
    PartialStateTransition, /**< Group transition partially progressed; retry is supported */
    ResourceFailure,        /**< Underlying RTOS/memory resource operation failed */
    NotAllowedInIsr,        /**< Operation is not allowed from ISR context */
};

/**
 * @brief Aggregate lifecycle error that can optionally embed child error details.
 *
 * @tparam ChildErrorTs Child error types of nested lifecycle components.
 */
template <typename... ChildErrorTs>
struct AggregateLifecycleError {
    LifecycleErrorType error = LifecycleErrorType::InvalidState;
    etl::optional<etl::tuple<etl::optional<ChildErrorTs>...>> child_errors;

    static AggregateLifecycleError FromChildErrors(const etl::tuple<etl::optional<ChildErrorTs>...>& child_errors_in) {
        AggregateLifecycleError out;
        out.error = LifecycleErrorType::AggregateError;
        out.child_errors = child_errors_in;
        return out;
    }

    static bool HasAnyChildErrors(const etl::tuple<etl::optional<ChildErrorTs>...>& child_errors_in) {
        return HasAnyChildErrorsImpl(child_errors_in, std::index_sequence_for<ChildErrorTs...>{});
    }

    static etl::optional<AggregateLifecycleError> TryFromChildErrors(
        const etl::tuple<etl::optional<ChildErrorTs>...>& child_errors_in) {
        if (!HasAnyChildErrors(child_errors_in)) {
            return etl::nullopt;
        }
        return FromChildErrors(child_errors_in);
    }

   private:
    template <size_t... Indexes>
    static bool HasAnyChildErrorsImpl(const etl::tuple<etl::optional<ChildErrorTs>...>& child_errors_in,
                                      std::index_sequence<Indexes...>) {
        return (... || etl::get<Indexes>(child_errors_in).has_value());
    }
};

/**
 * @brief Leaf specialization with compact storage and implicit enum conversion.
 */
template <>
struct AggregateLifecycleError<> {
    LifecycleErrorType error = LifecycleErrorType::InvalidState;

    constexpr AggregateLifecycleError() = default;
    constexpr AggregateLifecycleError(LifecycleErrorType error_in) : error(error_in) {}

    static constexpr AggregateLifecycleError<> FromType(LifecycleErrorType error_in) {
        return AggregateLifecycleError<>(error_in);
    }
};

/**
 * @brief Leaf lifecycle error alias (no child error details).
 */
using LifecycleError = AggregateLifecycleError<>;

/**
 * @brief Sentinel local configuration for aggregate components with no local config.
 */
struct NoLocalConfig {};

/**
 * @brief Configuration bundle for aggregate lifecycle components.
 *
 * Keeps local aggregate configuration separate from child configuration tuple.
 *
 * @tparam LocalConfigT Aggregate-local configuration type
 * @tparam ChildConfigTs Child component configuration types
 */
template <typename LocalConfigT, typename... ChildConfigTs>
struct AggregateConfigBundle {
    LocalConfigT local{};
    etl::tuple<ChildConfigTs...> children{};
};

}  // namespace spiopen::lifecycle
