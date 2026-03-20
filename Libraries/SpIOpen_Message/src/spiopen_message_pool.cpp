/*
SpIOpen Message Frame Pool : Implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_message_pool.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace spiopen::message {

namespace {

constexpr size_t AlignUpBytes(size_t value, size_t alignment) {
    return (alignment == 0U) ? value : ((value + alignment - 1U) / alignment) * alignment;
}

}  // namespace

FramePool::FramePool()
    : active_storage_(),
      state_(LifecycleState::Unconfigured),
      config_{0U, 0U, etl::span<uint8_t>(), nullptr},
      available_messages_queue_(nullptr),
      owned_storage_(nullptr) {}

etl::expected<void, LifecycleError> FramePool::Configure(const FramePoolConfig& config) {
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
    active_storage_ = etl::span<uint8_t>();
    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<FramePool::ConfigType, FramePool::ErrorType> FramePool::ValidateAndNormalizeConfiguration(
    const ConfigType& config) {
    if (config.frame_buffer_size == 0U) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }

    const size_t required_bytes = config.message_count * config.frame_buffer_size;
    const size_t required_full_layout =
        AlignUpBytes(config.message_count * sizeof(FrameMessage*), alignof(FrameMessage)) +
        (config.message_count * sizeof(FrameMessage)) + required_bytes;

    if (!config.pool_storage.empty()) {
        if (config.pool_storage.size() < required_full_layout) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
    } else {
        if (required_full_layout == 0U) {
            // Zero-count pools are valid with no storage.
        } else if (!MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
    }

    return config;
}

etl::expected<void, LifecycleError> FramePool::Initialize() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Initializing, std::memory_order_release);

    if (config_.message_count == 0U) {
        // #NOTE: Zero-capacity pool is treated as initialized but permanently exhausted.
        available_messages_queue_ = nullptr;
        state_.store(LifecycleState::Inactive, std::memory_order_release);
        return {};
    }

    const size_t queue_storage_size = config_.message_count * sizeof(FrameMessage*);
    const size_t messages_offset = AlignUpBytes(queue_storage_size, alignof(FrameMessage));
    const size_t messages_size = config_.message_count * sizeof(FrameMessage);
    const size_t frame_buffers_offset = messages_offset + messages_size;
    const size_t frame_buffers_size = config_.message_count * config_.frame_buffer_size;
    const size_t total_required_size = frame_buffers_offset + frame_buffers_size;

    if (!config_.pool_storage.empty()) {
        if (config_.pool_storage.size() < total_required_size) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
        active_storage_ = config_.pool_storage;
    } else {
        if (!MESSAGE_ALLOW_HEAP_ALLOCATION_AT_INIT) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
        }
        owned_storage_ = new (std::nothrow) uint8_t[total_required_size];
        if (owned_storage_ == nullptr) {
            state_.store(LifecycleState::Configured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
        active_storage_ = etl::span<uint8_t>(owned_storage_, total_required_size);
    }

    // ONCE WE HAVE POTENTIALLY ALLOCATED MEMORY, WE CAN'T FALL BACK TO THE CONFIGURED STATE WITHOUT DE-ALLOCATING THE
    // MEMORY.

    osMessageQueueAttr_t queue_attr = {};
    queue_attr.name = ((config_.name != nullptr) && (config_.name[0] != '\0')) ? config_.name : "spiopen-frame-pool";
    queue_attr.attr_bits = 0U;
    queue_attr.cb_mem = nullptr;  //#TODO provide this from internal array
    queue_attr.cb_size = 0U;
    queue_attr.mq_mem = active_storage_.data();
    queue_attr.mq_size = static_cast<uint32_t>(queue_storage_size);

    available_messages_queue_ =
        osMessageQueueNew(static_cast<uint32_t>(config_.message_count), sizeof(FrameMessage*), &queue_attr);
    if (available_messages_queue_ == nullptr) {
        // clean up before abort
        if (owned_storage_ != nullptr) {
            delete[] owned_storage_;
            owned_storage_ = nullptr;
        }
        active_storage_ = etl::span<uint8_t>();
        state_.store(LifecycleState::Unconfigured, std::memory_order_release);
        return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
    }

    uint8_t* base = active_storage_.data();
    auto* messages_region = reinterpret_cast<FrameMessage*>(base + messages_offset);
    uint8_t* frame_buffers_region = base + frame_buffers_offset;

    for (size_t i = 0U; i < config_.message_count; ++i) {
        uint8_t* message_frame_buffer = frame_buffers_region + (i * config_.frame_buffer_size);
        auto frame_storage = etl::span<uint8_t>(message_frame_buffer, config_.frame_buffer_size);
        auto* message = new (&messages_region[i]) FrameMessage(*this, frame_storage);
        const osStatus_t put_status = osMessageQueuePut(available_messages_queue_, &message, 0U, 0U);
        if (put_status != osOK) {
            // #TODO: Add centralized fault logging when queue warmup fails unexpectedly.
            // clean up before abort
            if (owned_storage_ != nullptr) {
                delete[] owned_storage_;
                owned_storage_ = nullptr;
            }
            active_storage_ = etl::span<uint8_t>();
            state_.store(LifecycleState::Unconfigured, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
    }

    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FramePool::Start() {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Starting, std::memory_order_release);
    state_.store(LifecycleState::Active, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FramePool::Stop() {
    if (state_.load(std::memory_order_relaxed) != LifecycleState::Active) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Inactive, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FramePool::Deinitialize() {
    if (state_.load(std::memory_order_relaxed) != LifecycleState::Inactive) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }

    if (config_.message_count > 0U) {
        if (available_messages_queue_ == nullptr) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
        if (osMessageQueueGetCount(available_messages_queue_) != config_.message_count) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        }
    }

    state_.store(LifecycleState::Deinitializing, std::memory_order_release);

    if (available_messages_queue_ != nullptr) {
        const osStatus_t delete_status = osMessageQueueDelete(available_messages_queue_);
        if (delete_status == osErrorResource) {
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        } else if (delete_status == osErrorParameter) {
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::ResourceFailure));
        } else if (delete_status == osErrorISR) {
            state_.store(LifecycleState::Inactive, std::memory_order_release);
            return etl::unexpected(LifecycleError(LifecycleErrorType::NotAllowedInIsr));
        } else if (delete_status != osOK) {
            // #TODO: Route impossible lifecycle faults through centralized fault logging hook.
            std::abort();
        }
        available_messages_queue_ = nullptr;
    }

    active_storage_ = etl::span<uint8_t>();
    if (owned_storage_ != nullptr) {
        delete[] owned_storage_;
        owned_storage_ = nullptr;
    }

    state_.store(LifecycleState::Configured, std::memory_order_release);
    return {};
}

etl::expected<void, LifecycleError> FramePool::Unconfigure() {
    // ILifecycleComponent::Unconfigure: clear configuration from Configured → Unconfigured only (see
    // spiopen_lifecycle).
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Configured) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidState));
    }
    state_.store(LifecycleState::Unconfiguring, std::memory_order_release);
    config_ = FramePoolConfig{0U, 0U, etl::span<uint8_t>(), nullptr};
    state_.store(LifecycleState::Unconfigured, std::memory_order_release);
    return {};
}

etl::expected<FrameMessage*, FramePoolError> FramePool::AllocateFrameMessage(
    MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle, uint32_t timeout_ticks) {
    const LifecycleState current_state = state_.load(std::memory_order_relaxed);
    if (current_state != LifecycleState::Active) {
        return etl::unexpected(FramePoolError::InvalidState);
    }
    if (config_.message_count == 0U) {
        return etl::unexpected(FramePoolError::PoolExhausted);
    }
    if (available_messages_queue_ == nullptr) {
        return etl::unexpected(FramePoolError::ResourceFailure);
    }

    FrameMessage* message = nullptr;
    const osStatus_t get_status = osMessageQueueGet(available_messages_queue_, &message, nullptr, timeout_ticks);
    if (get_status != osOK) {
        if ((get_status == osErrorTimeout) || (get_status == osErrorResource)) {
            return etl::unexpected(FramePoolError::PoolExhausted);
        }
        return etl::unexpected(FramePoolError::QueueFailure);
    }
    if (message == nullptr) {
        return etl::unexpected(FramePoolError::QueueFailure);
    }

    auto allocate_ret = message->AllocateToPublisher(message_type, publisher_handle, 1U);
    if (!allocate_ret) {
        // Can only happen in parameters are wrong, or if the message is in the wrong state. Both should be guaranteed
        // by testing.
        // #TODO add global log
        std::abort();
    }
    return message;
}

void FramePool::RequeueFrameMessage(FrameMessage* message) {
    if (message == nullptr) {
        std::abort();
    }
    if (config_.message_count == 0U) {
        // #TODO add global log
        std::abort();
    }
    if (available_messages_queue_ == nullptr) {
        // #TODO add global log
        std::abort();
    }
    const osStatus_t put_status = osMessageQueuePut(available_messages_queue_, &message, 0U, 0U);
    if (put_status != osOK) {
        // #TODO add global log
        std::abort();
    }
}

LifecycleState FramePool::GetState() const { return state_.load(std::memory_order_relaxed); }

size_t FramePool::GetFrameBufferSize() const { return config_.frame_buffer_size; }

size_t FramePool::GetMessageCount() const { return config_.message_count; }

}  // namespace spiopen::message
