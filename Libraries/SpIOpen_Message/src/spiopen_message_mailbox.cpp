/*
SpIOpen Message Frame Mailbox : Implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_message_mailbox.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace spiopen::message {

namespace {

FrameMailboxError MapQueueStatusToMailboxError(osStatus_t status) {
    if ((status == osErrorTimeout) || (status == osErrorResource)) {
        return FrameMailboxError::QueueTimeout;
    }
    return FrameMailboxError::QueueFailure;
}

}  // namespace

FrameMailbox::FrameMailbox()
    : queue_control_block_storage_{},
      state_(LifecycleState::Unconfigured),
      config_{0U, etl::span<uint8_t>()},
      queue_handle_(nullptr) {}

etl::expected<void, LifecycleError> FrameMailbox::Configure(const FrameMailboxConfig& config) {
    LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if ((current_state != LifecycleState::Unconfigured) && (current_state != LifecycleState::Configured)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Configuring, std::memory_order_release);

    auto normalized_config_ret = ValidateAndNormalizeConfiguration(config);
    if (!normalized_config_ret) {
        state_.store(LifecycleState::Unconfigured, std::memory_order_release);
        return etl::unexpected(normalized_config_ret.error());
    }

    config_ = *normalized_config_ret;
    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<FrameMailbox::ConfigType, FrameMailbox::ErrorType> FrameMailbox::ValidateAndNormalizeConfiguration(
    const ConfigType& config) {
    if ((config.depth == 0U) || (config.depth > MESSAGE_MAILBOX_MAX_DEPTH)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }
    if (!MESSAGE_MAILBOX_DEPTH_CONFIGURABLE && (config.depth != MESSAGE_MAILBOX_MAX_DEPTH)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }

    const size_t queue_storage_required_bytes = config.depth * sizeof(FrameMessage*);
    if (!config.queue_storage.empty() && (config.queue_storage.size() < queue_storage_required_bytes)) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }

    ConfigType normalized_config = config;
    if (!MESSAGE_MAILBOX_DEPTH_CONFIGURABLE) {
        normalized_config.depth = MESSAGE_MAILBOX_MAX_DEPTH;
    }
    return normalized_config;
}

etl::expected<void, LifecycleError> FrameMailbox::Initialize() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }

    state_.store(LifecycleState::Initializing, std::memory_order_release);

    osMessageQueueAttr_t queue_attr = {};
    queue_attr.name = ((config_.name != nullptr) && (config_.name[0] != '\0')) ? config_.name : "spiopen-mailbox";
    queue_attr.attr_bits = 0U;
    queue_attr.cb_mem = queue_control_block_storage_.data();
    queue_attr.cb_size = static_cast<uint32_t>(queue_control_block_storage_.size());
    queue_attr.mq_mem = nullptr;
    queue_attr.mq_size = 0U;

    if (!config_.queue_storage.empty()) {
        queue_attr.mq_mem = config_.queue_storage.data();
        queue_attr.mq_size = static_cast<uint32_t>(config_.queue_storage.size());
    }

    queue_handle_ = osMessageQueueNew(static_cast<uint32_t>(config_.depth), sizeof(FrameMessage*), &queue_attr);
    if (queue_handle_ == nullptr) {
        state_.store(LifecycleState::Configured, std::memory_order_release);
        return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
    }

    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameMailbox::Start() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }

    // state_.store(LifecycleState::Starting, std::memory_order_release); //skip this step in this case
    state_.store(LifecycleState::Active, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameMailbox::Stop() {
    if (state_.load(std::memory_order_relaxed) != LifecycleState::Active) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }

    // state_.store(LifecycleState::Stopping, std::memory_order_release); //skip this step in this case
    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameMailbox::Deinitialize() {
    if (state_.load(std::memory_order_relaxed) != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    // Defensive invariant check: Inactive implies queue should exist until deinit succeeds.
    if (queue_handle_ == nullptr) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
    }

    if (osMessageQueueGetCount(queue_handle_) != 0U) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
    }

    state_.store(LifecycleState::Deinitializing, std::memory_order_release);
    const osStatus_t delete_status = osMessageQueueDelete(queue_handle_);
    if (delete_status == osErrorISR) {
        state_.store(LifecycleState::Inactive, std::memory_order_release);
        return etl::unexpected(LifecycleError(LifecycleErrorType::NotAllowedInIsr));
    } else if (delete_status == osErrorResource) {
        state_.store(LifecycleState::Inactive, std::memory_order_release);
        return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
    } else if (delete_status == osErrorParameter) {
        // Queue handle is invalid from RTOS perspective; finalize local state as deinitialized.
        queue_handle_ = nullptr;
        state_.store(LifecycleState::Configured, std::memory_order_release);
        return {};
    } else if (delete_status != osOK) {
        // #TODO: Route impossible lifecycle faults through centralized fault logging hook.
        std::abort();
    }
    queue_handle_ = nullptr;
    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FrameMailbox::Unconfigure() {
    // ILifecycleComponent::Unconfigure: clear configuration from Configured → Unconfigured only (see
    // spiopen_lifecycle).
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }

    state_.store(LifecycleState::Unconfiguring, std::memory_order_release);
    config_ = FrameMailboxConfig{0U, etl::span<uint8_t>(), nullptr};
    state_.store(LifecycleState::Unconfigured, std::memory_order_release);
    return {};
}

etl::expected<size_t, FrameMailboxError> FrameMailbox::DrainAndReleaseAll() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if ((current_state != LifecycleState::Inactive) && (current_state != LifecycleState::Active)) {
        return etl::unexpected(FrameMailboxError::InvalidState);
    }
    // Defensive invariant check: initialized states must have a valid queue handle.
    if (queue_handle_ == nullptr) {
        return etl::unexpected(FrameMailboxError::ResourceFailure);
    }

    size_t drained_count = 0U;
    FrameMailboxError first_error = FrameMailboxError::QueueFailure;
    bool had_error = false;

    while (true) {
        auto dequeue_ret = Dequeue(0U);
        if (dequeue_ret) {
            (*dequeue_ret)->Release();
            ++drained_count;
            continue;
        }
        if (dequeue_ret.error() == FrameMailboxError::QueueTimeout) {
            break;
        }
        if (!had_error) {
            first_error = dequeue_ret.error();
            had_error = true;
        }
        break;
    }

    if (had_error) {
        return etl::unexpected(first_error);
    }
    return drained_count;
}

etl::expected<void, FrameMailboxError> FrameMailbox::Enqueue(FrameMessage* message, uint32_t timeout_ticks) {
    if (message == nullptr) {
        return etl::unexpected(FrameMailboxError::InvalidArgument);
    }

    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Active) {
        return etl::unexpected(FrameMailboxError::InvalidState);
    }
    // Defensive invariant check: Active implies queue resource exists.
    if (queue_handle_ == nullptr) {
        return etl::unexpected(FrameMailboxError::ResourceFailure);
    }

    auto acquire_ret = message->AcquireReference();
    if (!acquire_ret) {
        return etl::unexpected(FrameMailboxError::PreconditionFailed);
    }

    const osStatus_t queue_status = osMessageQueuePut(queue_handle_, &message, 0U, timeout_ticks);
    if (queue_status == osOK) {
        return {};
    }

    // Roll back the reference acquired by mailbox before enqueue attempt.
    message->Release();
    return etl::unexpected(MapQueueStatusToMailboxError(queue_status));
}

etl::expected<FrameMessage*, FrameMailboxError> FrameMailbox::Dequeue(uint32_t timeout_ticks) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if ((current_state != LifecycleState::Inactive) && (current_state != LifecycleState::Active)) {
        return etl::unexpected(FrameMailboxError::InvalidState);
    }
    // Defensive invariant check: initialized states must have a valid queue handle.
    if (queue_handle_ == nullptr) {
        return etl::unexpected(FrameMailboxError::ResourceFailure);
    }

    FrameMessage* dequeued_message = nullptr;
    const osStatus_t queue_status = osMessageQueueGet(queue_handle_, &dequeued_message, nullptr, timeout_ticks);
    if (queue_status == osOK) {
        if (dequeued_message == nullptr) {
            return etl::unexpected(FrameMailboxError::QueueFailure);
        }
        return dequeued_message;
    }

    return etl::unexpected(MapQueueStatusToMailboxError(queue_status));
}

LifecycleState FrameMailbox::GetState() const { return state_.load(std::memory_order_relaxed); }

}  // namespace spiopen::message
