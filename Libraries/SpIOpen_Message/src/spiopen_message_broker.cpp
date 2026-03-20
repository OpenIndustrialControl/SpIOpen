/*
SpIOpen Message Frame Broker : Implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_message_broker.h"

#include <cstdlib>

namespace spiopen::message {

FrameBroker::FrameBroker()
    : config_{nullptr, 0U, 0U, etl::span<uint8_t>(), FrameMailboxConfig{0U, etl::span<uint8_t>()}},
      thread_control_block_storage_{},
      active_thread_stack_storage_(),
      state_(LifecycleState::Unconfigured),
      thread_id_(nullptr),
      owned_thread_stack_memory_(nullptr),
      inbox_mailbox_(),
      subscribers_(),
      enqueue_error_count_(0U) {
    subscribers_.fill(nullptr);
}

etl::expected<void, LifecycleError> FrameBroker::Configure(const FrameBrokerConfig& config) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Unconfigured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Configuring, std::memory_order_release);

    auto normalized_config_ret = ValidateAndNormalizeConfiguration(config);
    if (!normalized_config_ret) {
        state_.store(LifecycleState::Unconfigured, std::memory_order_release);
        return etl::unexpected(normalized_config_ret.error());
    }
    config_ = *normalized_config_ret;
    if (!IsInboxMailboxDisabled()) {
        auto mailbox_ret = inbox_mailbox_.Configure(config_.inbox_mailbox_config);
        if (!mailbox_ret) {
            state_.store(LifecycleState::Unconfigured, std::memory_order_release);
            return etl::unexpected(LifecycleError(mailbox_ret.error().error));
        }
    }
    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<FrameBroker::ConfigType, FrameBroker::ErrorType> FrameBroker::ValidateAndNormalizeConfiguration(
    const ConfigType& config) {
    if ((config.thread_stack_size_bytes == 0U) ||
        (config.thread_stack_size_bytes > MESSAGE_BROKER_THREAD_MAX_STACK_SIZE)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }
    if (!config.thread_stack_storage.empty() && (config.thread_stack_storage.size() < config.thread_stack_size_bytes)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }
    if (!MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT && config.thread_stack_storage.empty()) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }

    ConfigType normalized_config = config;
    if (config.inbox_mailbox_config.depth != 0U) {
        auto mailbox_config_ret = inbox_mailbox_.ValidateAndNormalizeConfiguration(config.inbox_mailbox_config);
        if (!mailbox_config_ret) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
        normalized_config.inbox_mailbox_config = *mailbox_config_ret;
    }
    return normalized_config;
}

etl::expected<void, LifecycleError> FrameBroker::Initialize() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Initializing, std::memory_order_release);

    if (!config_.thread_stack_storage.empty()) {
        active_thread_stack_storage_ =
            etl::span<uint8_t>(config_.thread_stack_storage.data(), config_.thread_stack_size_bytes);
    } else {
        if (!MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
        }
        owned_thread_stack_memory_ = static_cast<uint8_t*>(std::malloc(config_.thread_stack_size_bytes));
        if (owned_thread_stack_memory_ == nullptr) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
        active_thread_stack_storage_ = etl::span<uint8_t>(owned_thread_stack_memory_, config_.thread_stack_size_bytes);
    }

    if (!IsInboxMailboxDisabled()) {
        auto mailbox_ret = inbox_mailbox_.Initialize();
        if (!mailbox_ret) {
            if (owned_thread_stack_memory_ != nullptr) {
                std::free(owned_thread_stack_memory_);
                owned_thread_stack_memory_ = nullptr;
            }
            active_thread_stack_storage_ = etl::span<uint8_t>();
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(mailbox_ret.error().error));
        }
    }

    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameBroker::Start() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Starting, std::memory_order_release);

    if (!IsInboxMailboxDisabled()) {
        auto mailbox_ret = inbox_mailbox_.Start();
        if (!mailbox_ret) {
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(mailbox_ret.error().error));
        }
        // new thread is created in Start() not Initialzie() because CMSIS does not allow you to create a suspended
        // thread, and the resources for the thread are all already created by initialize()
        osThreadAttr_t thread_attr = {};
        thread_attr.name = config_.thread_name;
        thread_attr.priority = static_cast<osPriority_t>(config_.thread_priority);
        thread_attr.cb_mem = thread_control_block_storage_.data();
        thread_attr.cb_size = static_cast<uint32_t>(thread_control_block_storage_.size());
        thread_attr.stack_mem = active_thread_stack_storage_.data();
        thread_attr.stack_size = config_.thread_stack_size_bytes;

        thread_id_ = osThreadNew(ThreadEntry, this, &thread_attr);
        if (thread_id_ == nullptr) {
            inbox_mailbox_.Stop();
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
    }

    state_.store(LifecycleState::Active, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameBroker::Stop() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Active) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Stopping, std::memory_order_release);

    if (thread_id_ != nullptr) {
        osThreadTerminate(thread_id_);
        thread_id_ = nullptr;
    }

    if (!IsInboxMailboxDisabled()) {
        inbox_mailbox_.Stop();
        inbox_mailbox_.DrainAndReleaseAll();
    }

    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameBroker::Deinitialize() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Deinitializing, std::memory_order_release);

    if (!IsInboxMailboxDisabled()) {
        auto mailbox_ret = inbox_mailbox_.Deinitialize();
        if (!mailbox_ret) {
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(mailbox_ret.error().error));
        }
    }

    if (owned_thread_stack_memory_ != nullptr) {
        std::free(owned_thread_stack_memory_);
        owned_thread_stack_memory_ = nullptr;
    }
    active_thread_stack_storage_ = etl::span<uint8_t>();

    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameBroker::Unconfigure() {
    // ILifecycleComponent::Unconfigure: clear configuration from Configured → Unconfigured only (see
    // spiopen_lifecycle).
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Unconfiguring, std::memory_order_release);

    if (!IsInboxMailboxDisabled()) {
        auto mailbox_unconfigure_ret = inbox_mailbox_.Unconfigure();
        if (!mailbox_unconfigure_ret) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(mailbox_unconfigure_ret.error().error));
        }
    }
    subscribers_.fill(nullptr);
    enqueue_error_count_.store(0U, std::memory_order_relaxed);
    if (owned_thread_stack_memory_ != nullptr) {
        std::free(owned_thread_stack_memory_);
        owned_thread_stack_memory_ = nullptr;
    }
    active_thread_stack_storage_ = etl::span<uint8_t>();
    config_ = FrameBrokerConfig{nullptr, 0U, 0U, etl::span<uint8_t>(), FrameMailboxConfig{0U, etl::span<uint8_t>()}};

    state_.store(LifecycleState::Unconfigured, std::memory_order_release);
    return {};
}

etl::expected<void, FrameBrokerError> FrameBroker::Subscribe(subscriber::FrameSubscriberHandle_t* subscriber_handle) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if ((current_state != LifecycleState::Configured) && (current_state != LifecycleState::Inactive)) {
        return etl::unexpected(FrameBrokerError::InvalidState);
    }
    if (subscriber_handle == nullptr) {
        return etl::unexpected(FrameBrokerError::InvalidArgument);
    }
    if (subscriber_handle->mailbox == nullptr) {
        return etl::unexpected(FrameBrokerError::InvalidArgument);
    }

    for (auto& slot : subscribers_) {
        if (slot == nullptr) {
            slot = subscriber_handle;
            return {};
        }
    }
    return etl::unexpected(FrameBrokerError::SubscriptionTableFull);
}

etl::expected<void, FrameBrokerError> FrameBroker::Unsubscribe(
    const subscriber::FrameSubscriberHandle_t* subscriber_handle) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if ((current_state != LifecycleState::Configured) && (current_state != LifecycleState::Inactive)) {
        return etl::unexpected(FrameBrokerError::InvalidState);
    }
    if (subscriber_handle == nullptr) {
        return etl::unexpected(FrameBrokerError::InvalidArgument);
    }

    for (auto& slot : subscribers_) {
        if (slot == subscriber_handle) {
            slot = nullptr;
            return {};
        }
    }
    return etl::unexpected(FrameBrokerError::SubscriptionNotFound);
}

etl::expected<void, FrameBrokerError> FrameBroker::Publish(FrameMessage* message, uint32_t timeout_ticks) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Active) {
        return etl::unexpected(FrameBrokerError::InvalidState);
    }
    if (message == nullptr) {
        return etl::unexpected(FrameBrokerError::InvalidArgument);
    }

    if (IsInboxMailboxDisabled()) {
        return FanOutToSubscribers(message);
    }

    auto enqueue_ret = inbox_mailbox_.Enqueue(message, timeout_ticks);
    if (!enqueue_ret) {
        return etl::unexpected(FrameBrokerError::PublishFailed);
    }
    return {};
}

LifecycleState FrameBroker::GetState() const { return state_.load(std::memory_order_relaxed); }

uint32_t FrameBroker::GetEnqueueErrorCount() const { return enqueue_error_count_.load(std::memory_order_relaxed); }

void FrameBroker::ThreadEntry(void* context) {
    auto* broker = static_cast<FrameBroker*>(context);
    while (broker->state_.load(std::memory_order_relaxed) == LifecycleState::Active) {
        broker->RunLoop();
    }
}

void FrameBroker::RunLoop() {
    auto dequeue_ret = inbox_mailbox_.Dequeue(osWaitForever);
    if (!dequeue_ret) {
        return;
    }
    FrameMessage* message = *dequeue_ret;
    if (message != nullptr) {
        FanOutToSubscribers(message);
        message->Release();
    }
}

bool FrameBroker::IsInboxMailboxDisabled() const { return config_.inbox_mailbox_config.depth == 0U; }

etl::expected<void, FrameBrokerError> FrameBroker::FanOutToSubscribers(FrameMessage* message) {
    if (message == nullptr) {
        return etl::unexpected(FrameBrokerError::InvalidArgument);
    }
    const auto message_type = message->GetMessageType();

    for (auto* sub : subscribers_) {
        if (sub == nullptr) {
            continue;
        }
        if (!subscriber::AcceptsMessageType(*sub, message_type)) {
            continue;
        }
        if (sub->mailbox == nullptr) {
            continue;
        }
        auto enqueue_ret = sub->mailbox->Enqueue(message, 0U);
        if (!enqueue_ret) {
            sub->enqueue_error_count.fetch_add(1U, std::memory_order_relaxed);
            enqueue_error_count_.fetch_add(1U, std::memory_order_relaxed);
        }
    }
    return {};
}

}  // namespace spiopen::message
