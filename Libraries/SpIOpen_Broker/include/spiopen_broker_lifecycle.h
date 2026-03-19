/*
SpIOpen Broker Lifecycle : Shared lifecycle state enum for pool, mailbox, broker, and allocator.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "etl/expected.h"
#include "etl/optional.h"
#include "etl/tuple.h"

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
    Resetting,        /**< Reset to Configured state in progress */
    Mixed,            /**< (Allocator only) Underlying pools in non-uniform states */
    // MAYBE ADD LATER:Faulted,          /**< Fatal error state; no further operations allowed */
    // MAYBE ADD LATER:Recovering,       /**< Recovering from fault state */
};

/**
 * @brief Generic primary error codes for lifecycle transition interface methods.
 */
enum class LifecycleErrorType : uint8_t {
    InvalidArgument = 1,    /**< Invalid argument passed to lifecycle transition */
    InvalidConfiguration,   /**< Configuration is invalid and must be rejected*/
    InvalidState,           /**< Lifecycle transition called in invalid current state */
    AggregateError,         /**< Error originated from one or more child lifecycle components */
    PartialStateTransition, /**< Group transition partially progressed; retry is supported */
    NotConfigured,          /**< Operation requires prior successful Configure() */
    NotInitialized,         /**< Operation requires prior successful Initialize() */
    NotActive,              /**< Operation requires prior successful Start() */
    ResourceFailure,        /**< Underlying RTOS/memory resource operation failed */
    NotAllowedInIsr,        /**< Operation is not allowed from ISR context */
};

/**
 * @brief Aggregate lifecycle error that can optionally embed child error details.
 *
 * @tparam ChildErrorTs Child error types of nested lifecycle components.
 * For leaf components, this pack is empty and only the primary error code is used.
 */
template <typename... ChildErrorTs>
struct AggregateLifecycleError {
    LifecycleErrorType error = LifecycleErrorType::InvalidState; /**< Primary lifecycle error code */
    etl::optional<etl::tuple<etl::optional<ChildErrorTs>...>>
        child_errors; /**< Optional per-child error details (position maps to child order) */

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
    LifecycleErrorType error = LifecycleErrorType::InvalidState; /**< Primary lifecycle error code */

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
 * @brief Sentinel aggregate configuration for aggregate components with no local config.
 */
struct NoAggregateConfig {};

/**
 * @brief Configuration bundle for aggregate lifecycle components.
 *
 * Keeps aggregate-local configuration separate from the tuple of child configurations.
 *
 * @tparam AggregateConfigT Aggregate-local configuration type
 * @tparam ChildConfigTs Child component configuration types
 */
template <typename AggregateConfigT, typename... ChildConfigTs>
struct AggregateConfigBundle {
    AggregateConfigT local{};
    etl::tuple<ChildConfigTs...> children{};
};

/**
 * @brief Pure-virtual interface for broker components following the common lifecycle contract.
 *
 * Enforces the common lifecycle transition API with shared lifecycle errors.
 *
 * @tparam ConfigT Class-specific configuration structure
 * @tparam TError Class-specific error type (leaf or aggregate lifecycle error)
 */
template <typename ConfigT, typename TError>
class ILifecycleComponent {
   public:
    using ConfigType = ConfigT;
    using ErrorType = TError;

    virtual ~ILifecycleComponent() = default;

    /**
     * @brief Applies configuration in Unconfigured/Configured states.
     * @param config Class-specific runtime configuration
     * @return Success when configuration is accepted
     */
    virtual etl::expected<void, TError> Configure(const ConfigT& config) = 0;

    /**
     * @brief Validates and normalizes a proposed component configuration.
     *
     * Implementations should use this as the single source of configuration validation.
     * Configuration-specific failures should return LifecycleErrorType::InvalidConfiguration.
     *
     * @param config Proposed component configuration
     * @return Normalized configuration on success
     */
    virtual etl::expected<ConfigT, TError> ValidateAndNormalizeConfiguration(const ConfigT& config) = 0;

    /**
     * @brief Initializes resources and transitions to Inactive on success.
     * @return Success on initialization
     */
    virtual etl::expected<void, TError> Initialize() = 0;

    /**
     * @brief Starts runtime behavior and transitions to Active on success.
     * @return Success on start
     */
    virtual etl::expected<void, TError> Start() = 0;

    /**
     * @brief Stops runtime behavior and transitions to Inactive on success.
     * @return Success on stop
     */
    virtual etl::expected<void, TError> Stop() = 0;

    /**
     * @brief Deinitializes resources and transitions to Configured on success.
     * @return Success on deinitialization
     */
    virtual etl::expected<void, TError> Deinitialize() = 0;

    /**
     * @brief Clears the configuration of the component and transitions to Unconfigured on success.
     After being called, any memory allocated to the component in a previous Configure() call may be freed.
     * @return Success on reset
     */
    virtual etl::expected<void, TError> Reset() = 0;

    /**
     * @brief Returns the current shared lifecycle state.
     * @return Current lifecycle state
     */
    virtual LifecycleState GetState() const = 0;
};

/**
 * @brief Interface helper for lifecycle components composed from child lifecycle components.
 *
 * This interface derives aggregate ConfigType and ErrorType from child component types.
 * ConfigType is an AggregateConfigBundle containing aggregate-local config and an
 * etl::tuple of child configs. ErrorType is an AggregateLifecycleError of child errors.
 *
 * @tparam AggregateConfigT Aggregate-local configuration type (use NoAggregateConfig when none)
 * @tparam ChildComponentsT Child lifecycle component types
 */
template <typename AggregateConfigT = NoAggregateConfig, typename... ChildComponentsT>
class IAggregateLifecycleComponent
    : public ILifecycleComponent<AggregateConfigBundle<AggregateConfigT, typename ChildComponentsT::ConfigType...>,
                                 AggregateLifecycleError<typename ChildComponentsT::ErrorType...>> {
   public:
    using AggregateConfigType = AggregateConfigT;
    using ChildConfigTuple = etl::tuple<typename ChildComponentsT::ConfigType...>;
    using ConfigType = AggregateConfigBundle<AggregateConfigType, typename ChildComponentsT::ConfigType...>;
    using ErrorType = AggregateLifecycleError<typename ChildComponentsT::ErrorType...>;
    using BaseType = ILifecycleComponent<ConfigType, ErrorType>;
    using ChildComponentsTuple = etl::tuple<ChildComponentsT&...>;
    using ChildErrorDetailTuple = etl::tuple<etl::optional<typename ChildComponentsT::ErrorType>...>;

    etl::expected<void, ErrorType> Configure(const ConfigType& config) override {
        auto normalized_config = ValidateAndNormalizeConfiguration(config);
        if (!normalized_config) {
            return etl::unexpected(normalized_config.error());
        }
        return ConfigureChildren(normalized_config->children);
    }

    etl::expected<void, ErrorType> Initialize() override { return InitializeChildren(); }

    etl::expected<void, ErrorType> Start() override { return StartChildren(); }

    etl::expected<void, ErrorType> Stop() override { return StopChildren(); }

    etl::expected<void, ErrorType> Deinitialize() override { return DeinitializeChildren(); }

    etl::expected<void, ErrorType> Reset() override { return ResetChildren(); }

    ~IAggregateLifecycleComponent() override = default;

   protected:
    explicit IAggregateLifecycleComponent(ChildComponentsT&... child_components)
        : child_components_(child_components...) {}

    /**
     * @brief Optional pre-configure hook for aggregate components.
     *
     * Default behavior validates and normalizes both local aggregate config and children.
     * Overriding classes can add validation/normalization before or after calling helpers.
     *
     * @param config Proposed aggregate configuration
     * @return Normalized config on success, or high-level/aggregate configuration error
     */
    etl::expected<ConfigType, ErrorType> ValidateAndNormalizeConfiguration(const ConfigType& config) override {
        auto local_ret = ValidateAndNormalizeLocalConfiguration(config.local);
        if (!local_ret) {
            return etl::unexpected(local_ret.error());
        }
        auto children_ret = ValidateAndNormalizeChildrenConfigurations(config.children);
        if (!children_ret) {
            return etl::unexpected(children_ret.error());
        }
        return ConfigType{*local_ret, *children_ret};
    }

    /**
     * @brief Validates and normalizes aggregate-local configuration only.
     *
     * Default behavior is pass-through.
     *
     * @param local_config Proposed local aggregate configuration
     * @return Normalized local aggregate configuration, or lifecycle error
     */
    virtual etl::expected<AggregateConfigType, ErrorType> ValidateAndNormalizeLocalConfiguration(
        const AggregateConfigType& local_config) {
        return local_config;
    }

    /**
     * @brief Validates and normalizes each child configuration sequentially.
     *
     * Calls child_i.ValidateAndNormalizeConfiguration(config_i) in child order.
     * Always calls all children and aggregates all child validation errors.
     *
     * @param child_config Proposed child configuration tuple
     * @return Normalized child config tuple on success, or aggregated child error details
     */
    virtual etl::expected<ChildConfigTuple, ErrorType> ValidateAndNormalizeChildrenConfigurations(
        const ChildConfigTuple& child_config) {
        return ValidateAndNormalizeChildrenConfigurationsImpl(child_config, std::index_sequence_for<ChildComponentsT...>{});
    }

    etl::expected<void, ErrorType> ConfigureChildren(const ChildConfigTuple& child_config) {
        return FanOutToChildren(
            [&child_config](auto idx, auto& child) {
                return child.Configure(etl::get<decltype(idx)::value>(child_config));
            });
    }

    etl::expected<void, ErrorType> InitializeChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Initialize(); });
    }

    etl::expected<void, ErrorType> StartChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Start(); });
    }

    etl::expected<void, ErrorType> StopChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Stop(); });
    }

    etl::expected<void, ErrorType> DeinitializeChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Deinitialize(); });
    }

    etl::expected<void, ErrorType> ResetChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Reset(); });
    }

    ChildComponentsTuple& GetChildComponents() { return child_components_; }
    const ChildComponentsTuple& GetChildComponents() const { return child_components_; }

   private:
    template <size_t... Indexes>
    etl::expected<ChildConfigTuple, ErrorType> ValidateAndNormalizeChildrenConfigurationsImpl(
        const ChildConfigTuple& input_config, std::index_sequence<Indexes...>) {
        ChildConfigTuple normalized_config = input_config;
        ChildErrorDetailTuple child_errors{};

        (..., [&]() {
            auto child_config_ret =
                etl::get<Indexes>(child_components_).ValidateAndNormalizeConfiguration(etl::get<Indexes>(input_config));
            if (child_config_ret) {
                etl::get<Indexes>(normalized_config) = *child_config_ret;
            } else {
                etl::get<Indexes>(child_errors) = child_config_ret.error();
            }
        }());

        auto aggregate_error = ErrorType::TryFromChildErrors(child_errors);
        if (aggregate_error.has_value()) {
            return etl::unexpected(*aggregate_error);
        }
        return normalized_config;
    }

    /**
     * @brief Calls fn(integral_constant<I>, child_I) for each child, collecting per-child errors.
     * @tparam Fn Callable taking (std::integral_constant<size_t, I>, ChildComponent&)
     */
    template <typename Fn>
    etl::expected<void, ErrorType> FanOutToChildren(Fn&& fn) {
        return FanOutToChildrenImpl(std::index_sequence_for<ChildComponentsT...>{}, static_cast<Fn&&>(fn));
    }

    template <size_t... Indexes, typename Fn>
    etl::expected<void, ErrorType> FanOutToChildrenImpl(std::index_sequence<Indexes...>, Fn&& fn) {
        ChildErrorDetailTuple child_errors{};
        (..., [&]() {
            auto ret = fn(std::integral_constant<size_t, Indexes>{}, etl::get<Indexes>(child_components_));
            if (!ret) {
                etl::get<Indexes>(child_errors) = ret.error();
            }
        }());
        auto aggregate_error = ErrorType::TryFromChildErrors(child_errors);
        if (aggregate_error.has_value()) {
            return etl::unexpected(*aggregate_error);
        }
        return {};
    }

    ChildComponentsTuple child_components_;
};

}  // namespace spiopen::broker
