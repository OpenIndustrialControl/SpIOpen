/*
SpIOpen Message Frame Allocator : Implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_message_allocator.h"

namespace spiopen::message {

namespace {

constexpr const char* kDefaultCcPoolName = "spiopen-cc-frame-pool";
constexpr const char* kDefaultFdPoolName = "spiopen-fd-frame-pool";
constexpr const char* kDefaultXlPoolName = "spiopen-xl-frame-pool";

}  // namespace

FrameMessageAllocator::FrameMessageAllocator(FramePool& cc_pool, FramePool& fd_pool, FramePool& xl_pool)
    : AggregateBase(cc_pool, fd_pool, xl_pool) {}

etl::expected<FrameMessageAllocator::ChildConfigTuple, FrameMessageAllocator::ErrorType>
FrameMessageAllocator::ValidateAndNormalizeChildrenConfigurations(const ChildConfigTuple& child_config) {
    auto cc_ret = ValidateAndNormalizePoolConfig(etl::get<kCcPoolIndex>(child_config), format::CanMessageType::CanCc,
                                                 kDefaultCcPoolName);
    if (!cc_ret) {
        ErrorType err;
        err.error = cc_ret.error().error;
        return etl::unexpected(err);
    }
    auto fd_ret = ValidateAndNormalizePoolConfig(etl::get<kFdPoolIndex>(child_config), format::CanMessageType::CanFd,
                                                 kDefaultFdPoolName);
    if (!fd_ret) {
        ErrorType err;
        err.error = fd_ret.error().error;
        return etl::unexpected(err);
    }
    auto xl_ret = ValidateAndNormalizePoolConfig(etl::get<kXlPoolIndex>(child_config), format::CanMessageType::CanXl,
                                                 kDefaultXlPoolName);
    if (!xl_ret) {
        ErrorType err;
        err.error = xl_ret.error().error;
        return etl::unexpected(err);
    }
    return ChildConfigTuple{*cc_ret, *fd_ret, *xl_ret};
}

etl::expected<FramePoolConfig, LifecycleError> FrameMessageAllocator::ValidateAndNormalizePoolConfig(
    const FramePoolConfig& config, format::CanMessageType frame_type, const char* default_name) {
    FramePoolConfig effective_config = config;
    const size_t type_index = static_cast<size_t>(frame_type);

    const size_t min_frame_buffer_size = format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[type_index];
    const size_t max_message_count = MESSAGE_FRAME_POOL_MAX_FRAMES_BY_CAN_MESSAGE_TYPE[type_index];
    const bool is_type_enabled = (frame_type == format::CanMessageType::CanCc) ||
                                 (frame_type == format::CanMessageType::CanFd) ||
                                 ((frame_type == format::CanMessageType::CanXl) && MESSAGE_CAN_XL_ENABLED);

    if (effective_config.name == nullptr) {
        effective_config.name = default_name;
    }

    if (effective_config.frame_buffer_size < min_frame_buffer_size) {
        return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
    }

    if (!is_type_enabled) {
        if (effective_config.message_count != 0U) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
        return effective_config;
    }

    if (MESSAGE_FRAME_POOL_SIZE_CONFIGURABLE) {
        if (effective_config.message_count > max_message_count) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
    } else {
        if (effective_config.message_count != max_message_count) {
            return etl::unexpected(LifecycleError(LifecycleErrorType::InvalidConfiguration));
        }
    }

    return effective_config;
}

etl::expected<FrameMessage*, FramePoolError> FrameMessageAllocator::AllocateFrameMessage(
    size_t required_payload_bytes, MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle,
    uint32_t timeout_ticks) {
    FramePool* pool = SelectPoolForPayloadSize(required_payload_bytes);
    if (pool == nullptr) {
        return etl::unexpected(FramePoolError::UnsupportedFrameSize);
    }
    return pool->AllocateFrameMessage(message_type, publisher_handle, timeout_ticks);
}

etl::expected<FrameMessage*, FramePoolError> FrameMessageAllocator::AllocateFrameMessage(
    format::CanMessageType can_message_type, MessageType message_type,
    const publisher::FramePublisherHandle_t* publisher_handle, uint32_t timeout_ticks) {
    FramePool* pool = SelectPoolForCanMessageType(can_message_type);
    if (pool == nullptr) {
        return etl::unexpected(FramePoolError::UnsupportedFrameSize);
    }
    return pool->AllocateFrameMessage(message_type, publisher_handle, timeout_ticks);
}

FramePool* FrameMessageAllocator::SelectPoolForPayloadSize(size_t required_payload_bytes) {
    auto& children = GetChildComponents();
    if (required_payload_bytes <= format::MAX_CC_PAYLOAD_SIZE) {
        return &etl::get<kCcPoolIndex>(children);
    }
    if (required_payload_bytes <= format::MAX_FD_PAYLOAD_SIZE) {
        return &etl::get<kFdPoolIndex>(children);
    }
    if (MESSAGE_CAN_XL_ENABLED && (required_payload_bytes <= format::MAX_XL_PAYLOAD_SIZE)) {
        return &etl::get<kXlPoolIndex>(children);
    }
    return nullptr;
}

FramePool* FrameMessageAllocator::SelectPoolForCanMessageType(format::CanMessageType can_message_type) {
    auto& children = GetChildComponents();
    switch (can_message_type) {
        case format::CanMessageType::CanCc:
            return &etl::get<kCcPoolIndex>(children);
        case format::CanMessageType::CanFd:
            return &etl::get<kFdPoolIndex>(children);
        case format::CanMessageType::CanXl:
            return MESSAGE_CAN_XL_ENABLED ? &etl::get<kXlPoolIndex>(children) : nullptr;
        default:
            return nullptr;
    }
}

}  // namespace spiopen::message
