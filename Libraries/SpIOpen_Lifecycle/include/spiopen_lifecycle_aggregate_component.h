/*
SpIOpen Aggregate Lifecycle Interface : Lifecycle helper for composite components.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "etl/array.h"
#include "etl/optional.h"
#include "etl/tuple.h"
#include "spiopen_lifecycle_component.h"

namespace spiopen::lifecycle {

/**
 * @brief Steady / in-progress states for one coordinated transition across children.
 *
 * When children are not all identical but every child's state is one of the three fields
 * below, the aggregate reports @ref transitioning. Multiple bands may exist for the same
 * logical transition (e.g. Unconfigure) when valid entry steady states differ.
 */
struct LifecycleTransitionBand {
    LifecycleState starting;      /**< Steady state before the transition completes (entry side). */
    LifecycleState transitioning; /**< In-progress state while the transition is underway. */
    LifecycleState ending;        /**< Steady state after the transition completes (exit side). */
};

/**
 * @brief Table of transition bands used to classify aggregate lifecycle state from children.
 *
 * Order is not significant for matching; multiple rows may match ambiguously → Mixed.
 */
inline constexpr etl::array<LifecycleTransitionBand, 6> kLifecycleTransitionBands{{
    LifecycleTransitionBand{LifecycleState::Unconfigured, LifecycleState::Configuring, LifecycleState::Configured},
    LifecycleTransitionBand{LifecycleState::Configured, LifecycleState::Initializing, LifecycleState::Inactive},
    LifecycleTransitionBand{LifecycleState::Inactive, LifecycleState::Starting, LifecycleState::Active},
    LifecycleTransitionBand{LifecycleState::Active, LifecycleState::Stopping, LifecycleState::Inactive},
    LifecycleTransitionBand{LifecycleState::Inactive, LifecycleState::Deinitializing, LifecycleState::Configured},
    LifecycleTransitionBand{LifecycleState::Configured, LifecycleState::Unconfiguring, LifecycleState::Unconfigured},
}};

/**
 * @brief Classifies aggregate state from child ILifecycleComponent::GetState() results.
 *
 * - If every child shares the same state, returns that state.
 * - Else, if every child's state is one of @ref LifecycleTransitionBand::starting,
 *   @ref LifecycleTransitionBand::transitioning, or @ref LifecycleTransitionBand::ending for exactly one band,
 *   returns that band's @ref LifecycleTransitionBand::transitioning.
 * - Else (no band or multiple bands match), returns @ref LifecycleState::Mixed.
 *
 * @param child_states Pointer to @p child_count consecutive child states (read-only).
 * @param child_count Number of children (0 yields Mixed).
 */
inline LifecycleState ClassifyAggregateLifecycleState(const LifecycleState* child_states, size_t child_count) {
    if (child_count == 0U) {
        return LifecycleState::Mixed;
    }

    const LifecycleState first = child_states[0];
    bool all_same = true;
    for (size_t i = 1; i < child_count; ++i) {
        if (child_states[i] != first) {
            all_same = false;
            break;
        }
    }
    if (all_same) {
        return first;
    }

    auto state_in_band = [](LifecycleState s, const LifecycleTransitionBand& band) -> bool {
        return s == band.starting || s == band.transitioning || s == band.ending;
    };

    size_t match_count = 0;
    LifecycleState reported = LifecycleState::Mixed;
    for (const LifecycleTransitionBand& band : kLifecycleTransitionBands) {
        bool all_in_band = true;
        for (size_t i = 0; i < child_count; ++i) {
            if (!state_in_band(child_states[i], band)) {
                all_in_band = false;
                break;
            }
        }
        if (all_in_band) {
            // if multiple bands math they must all be the same transitioning state
            if (++match_count > 1U && reported != band.transitioning) {
                return LifecycleState::Mixed;
            }
            reported = band.transitioning;
        }
    }
    return LifecycleState::Mixed;
}

/**
 * @brief Lifecycle helper for components composed from child lifecycle components.
 *
 * ConfigType is an AggregateConfigBundle containing local config and a tuple of child configs.
 * ErrorType is AggregateLifecycleError of child error types.
 *
 * @tparam LocalConfigT Aggregate-local configuration type (use NoLocalConfig when none)
 * @tparam ChildComponentsT Child lifecycle component types
 */
template <typename LocalConfigT = NoLocalConfig, typename... ChildComponentsT>
class IAggregateLifecycleComponent
    : public ILifecycleComponent<AggregateConfigBundle<LocalConfigT, typename ChildComponentsT::ConfigType...>,
                                 AggregateLifecycleError<typename ChildComponentsT::ErrorType...>> {
   public:
    using LocalConfigType = LocalConfigT;
    using ChildConfigTuple = etl::tuple<typename ChildComponentsT::ConfigType...>;
    using ConfigType = AggregateConfigBundle<LocalConfigType, typename ChildComponentsT::ConfigType...>;
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
    etl::expected<void, ErrorType> Unconfigure() override { return UnconfigureChildren(); }

    LifecycleState GetState() const override {
        return ClassifyAggregateLifecycleStateFromChildren(std::index_sequence_for<ChildComponentsT...>{});
    }

    ~IAggregateLifecycleComponent() override = default;

   protected:
    explicit IAggregateLifecycleComponent(ChildComponentsT&... child_components)
        : child_components_(child_components...) {}

    /**
     * @brief Default normalize flow for aggregate components.
     *
     * Runs local and child normalization and recombines both into ConfigType.
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
     * @brief Validates and normalizes local aggregate configuration.
     *
     * Default behavior is pass-through.
     */
    virtual etl::expected<LocalConfigType, ErrorType> ValidateAndNormalizeLocalConfiguration(
        const LocalConfigType& local_config) {
        return local_config;
    }

    /**
     * @brief Validates and normalizes child configurations.
     *
     * Always calls all children and aggregates child validation errors.
     */
    virtual etl::expected<ChildConfigTuple, ErrorType> ValidateAndNormalizeChildrenConfigurations(
        const ChildConfigTuple& child_config) {
        return ValidateAndNormalizeChildrenConfigurationsImpl(child_config,
                                                              std::index_sequence_for<ChildComponentsT...>{});
    }

    etl::expected<void, ErrorType> ConfigureChildren(const ChildConfigTuple& child_config) {
        return FanOutToChildren([&child_config](auto idx, auto& child) {
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

    etl::expected<void, ErrorType> UnconfigureChildren() {
        return FanOutToChildren([](auto, auto& child) { return child.Unconfigure(); });
    }

    ChildComponentsTuple& GetChildComponents() { return child_components_; }
    const ChildComponentsTuple& GetChildComponents() const { return child_components_; }

   private:
    template <size_t... Indexes>
    LifecycleState ClassifyAggregateLifecycleStateFromChildren(std::index_sequence<Indexes...>) const {
        const etl::array<LifecycleState, sizeof...(Indexes)> states{etl::get<Indexes>(child_components_).GetState()...};
        return ClassifyAggregateLifecycleState(states.data(), states.size());
    }

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

}  // namespace spiopen::lifecycle
