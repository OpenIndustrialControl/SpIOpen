#include <gtest/gtest.h>

#include <cstdint>

#include "etl/array.h"
#include "spiopen_lifecycle.h"

namespace {

using spiopen::lifecycle::AggregateConfigBundle;
using spiopen::lifecycle::AggregateLifecycleError;
using spiopen::lifecycle::ClassifyAggregateLifecycleState;
using spiopen::lifecycle::IAggregateLifecycleComponent;
using spiopen::lifecycle::ILifecycleComponent;
using spiopen::lifecycle::LifecycleError;
using spiopen::lifecycle::LifecycleErrorType;
using spiopen::lifecycle::LifecycleState;
using spiopen::lifecycle::NoLocalConfig;

struct FakeConfig {
    int value = 0;
};

class FakeLeafComponent : public ILifecycleComponent<FakeConfig, LifecycleError> {
   public:
    explicit FakeLeafComponent(bool fail_normalize = false, LifecycleState state = LifecycleState::Unconfigured)
        : fail_normalize_(fail_normalize), state_(state) {}

    void SetStateForTest(LifecycleState state) { state_ = state; }

    etl::expected<void, ErrorType> Configure(const ConfigType& config) override {
        last_configure_value_ = config.value;
        return {};
    }

    etl::expected<ConfigType, ErrorType> ValidateAndNormalizeConfiguration(const ConfigType& config) override {
        if (fail_normalize_) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
        return ConfigType{config.value + 1};
    }

    etl::expected<void, ErrorType> Initialize() override { return {}; }
    etl::expected<void, ErrorType> Start() override { return {}; }
    etl::expected<void, ErrorType> Stop() override { return {}; }
    etl::expected<void, ErrorType> Deinitialize() override { return {}; }
    etl::expected<void, ErrorType> Unconfigure() override { return {}; }
    LifecycleState GetState() const override { return state_; }

    int last_configure_value() const { return last_configure_value_; }

   private:
    bool fail_normalize_;
    int last_configure_value_{0};
    LifecycleState state_;
};

struct AggregateLocalConfig {
    int base = 0;
};

class FakeAggregateComponent
    : public IAggregateLifecycleComponent<AggregateLocalConfig, FakeLeafComponent, FakeLeafComponent> {
   public:
    using Base = IAggregateLifecycleComponent<AggregateLocalConfig, FakeLeafComponent, FakeLeafComponent>;

    FakeAggregateComponent(FakeLeafComponent& a, FakeLeafComponent& b) : Base(a, b) {}

    etl::expected<ConfigType, ErrorType> ValidateAndNormalizeForTest(const ConfigType& config) {
        return this->ValidateAndNormalizeConfiguration(config);
    }

   protected:
    etl::expected<AggregateLocalConfig, ErrorType> ValidateAndNormalizeLocalConfiguration(
        const AggregateLocalConfig& local_config) override {
        return AggregateLocalConfig{local_config.base + 10};
    }
};

}  // namespace

TEST(SpIOpen_Lifecycle, LeafErrorImplicitConversion) {
    LifecycleError error = LifecycleErrorType::InvalidState;
    EXPECT_EQ(error.error, LifecycleErrorType::InvalidState);
}

TEST(SpIOpen_Lifecycle, AggregateErrorTryFromChildErrors) {
    etl::tuple<etl::optional<LifecycleError>, etl::optional<LifecycleError>> child_errors{};
    etl::get<1>(child_errors) = LifecycleError(LifecycleErrorType::InvalidConfiguration);

    auto aggregate_error = AggregateLifecycleError<LifecycleError, LifecycleError>::TryFromChildErrors(child_errors);
    ASSERT_TRUE(aggregate_error.has_value());
    EXPECT_EQ(aggregate_error->error, LifecycleErrorType::AggregateError);
}

TEST(SpIOpen_Lifecycle, AggregateConfigureUsesNormalizedChildren) {
    FakeLeafComponent child_a(false);
    FakeLeafComponent child_b(false);
    FakeAggregateComponent aggregate(child_a, child_b);

    AggregateConfigBundle<AggregateLocalConfig, FakeConfig, FakeConfig> config{
        AggregateLocalConfig{3}, etl::tuple<FakeConfig, FakeConfig>{FakeConfig{1}, FakeConfig{5}}};

    auto ret = aggregate.Configure(config);
    ASSERT_TRUE(ret);
    EXPECT_EQ(child_a.last_configure_value(), 2);
    EXPECT_EQ(child_b.last_configure_value(), 6);
}

TEST(SpIOpen_Lifecycle, AggregateNormalizeAggregatesAllChildErrors) {
    FakeLeafComponent child_a(true);
    FakeLeafComponent child_b(true);
    FakeAggregateComponent aggregate(child_a, child_b);

    AggregateConfigBundle<AggregateLocalConfig, FakeConfig, FakeConfig> config{
        AggregateLocalConfig{0}, etl::tuple<FakeConfig, FakeConfig>{FakeConfig{1}, FakeConfig{2}}};

    auto ret = aggregate.ValidateAndNormalizeForTest(config);
    EXPECT_FALSE(ret);
    if (!ret) {
        EXPECT_EQ(ret.error().error, LifecycleErrorType::AggregateError);
        ASSERT_TRUE(ret.error().child_errors.has_value());
        const auto& child_errors = *ret.error().child_errors;
        EXPECT_TRUE(etl::get<0>(child_errors).has_value());
        EXPECT_TRUE(etl::get<1>(child_errors).has_value());
    }
}

class FakeAggregateNoLocalComponent
    : public IAggregateLifecycleComponent<NoLocalConfig, FakeLeafComponent, FakeLeafComponent> {
   public:
    using Base = IAggregateLifecycleComponent<NoLocalConfig, FakeLeafComponent, FakeLeafComponent>;

    FakeAggregateNoLocalComponent(FakeLeafComponent& a, FakeLeafComponent& b) : Base(a, b) {}
};

TEST(SpIOpen_Lifecycle, NoLocalConfigBundleCompilesAndNormalizesChildren) {
    FakeLeafComponent child_a(false);
    FakeLeafComponent child_b(false);
    FakeAggregateNoLocalComponent aggregate(child_a, child_b);

    AggregateConfigBundle<NoLocalConfig, FakeConfig, FakeConfig> config{
        NoLocalConfig{}, etl::tuple<FakeConfig, FakeConfig>{FakeConfig{7}, FakeConfig{11}}};

    auto ret = aggregate.Configure(config);
    ASSERT_TRUE(ret);
    EXPECT_EQ(child_a.last_configure_value(), 8);
    EXPECT_EQ(child_b.last_configure_value(), 12);
}

TEST(SpIOpen_Lifecycle, ClassifyAggregate_AllSame_ReturnsThatState) {
    const etl::array<LifecycleState, 3> states{LifecycleState::Active, LifecycleState::Active, LifecycleState::Active};
    EXPECT_EQ(ClassifyAggregateLifecycleState(states.data(), states.size()), LifecycleState::Active);
}

TEST(SpIOpen_Lifecycle, ClassifyAggregate_ConfigureBand_ReturnsConfiguring) {
    const etl::array<LifecycleState, 2> states{LifecycleState::Unconfigured, LifecycleState::Configured};
    EXPECT_EQ(ClassifyAggregateLifecycleState(states.data(), states.size()), LifecycleState::Configuring);
}

TEST(SpIOpen_Lifecycle, ClassifyAggregate_AmbiguousBands_ReturnsMixed) {
    const etl::array<LifecycleState, 2> states{LifecycleState::Active, LifecycleState::Inactive};
    EXPECT_EQ(ClassifyAggregateLifecycleState(states.data(), states.size()), LifecycleState::Mixed);
}

TEST(SpIOpen_Lifecycle, ClassifyAggregate_IncompatibleMix_ReturnsMixed) {
    const etl::array<LifecycleState, 2> states{LifecycleState::Unconfigured, LifecycleState::Inactive};
    EXPECT_EQ(ClassifyAggregateLifecycleState(states.data(), states.size()), LifecycleState::Mixed);
}

TEST(SpIOpen_Lifecycle, AggregateGetState_DelegatesToClassifier) {
    FakeLeafComponent child_a(false, LifecycleState::Configured);
    FakeLeafComponent child_b(false, LifecycleState::Initializing);
    FakeAggregateNoLocalComponent aggregate(child_a, child_b);
    EXPECT_EQ(aggregate.GetState(), LifecycleState::Initializing);
}

TEST(SpIOpen_Lifecycle, AggregateGetState_AllChildrenSame) {
    FakeLeafComponent child_a(false, LifecycleState::Stopping);
    FakeLeafComponent child_b(false, LifecycleState::Stopping);
    FakeAggregateNoLocalComponent aggregate(child_a, child_b);
    EXPECT_EQ(aggregate.GetState(), LifecycleState::Stopping);
}
