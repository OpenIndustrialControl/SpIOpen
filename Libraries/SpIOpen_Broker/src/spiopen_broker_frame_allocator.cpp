/*
SpIOpen Broker Frame Allocator : Implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_broker_frame_allocator.h"

namespace spiopen::broker {

namespace {

constexpr const char* kDefaultCcPoolName = "spiopen-cc-frame-pool";
constexpr const char* kDefaultFdPoolName = "spiopen-fd-frame-pool";
constexpr const char* kDefaultXlPoolName = "spiopen-xl-frame-pool";

}  // namespace

FrameMessageAllocator::FrameMessageAllocator(FramePool& cc_pool, FramePool& fd_pool, FramePool& xl_pool)
    : cc_pool_(cc_pool), fd_pool_(fd_pool), xl_pool_(xl_pool) {}

etl::expected<void, LifecycleError> FrameMessageAllocator::Configure(const FramePoolConfig& cc_config,
                                                                     const FramePoolConfig& fd_config,
                                                                     const FramePoolConfig& xl_config) {
    auto effective_cc_config_ret =
        ValidateAndNormalizePoolConfig(cc_config, format::CanMessageType::CanCc, kDefaultCcPoolName);
    if (!effective_cc_config_ret) {
        return etl::unexpected(effective_cc_config_ret.error());
    }
    auto effective_fd_config_ret =
        ValidateAndNormalizePoolConfig(fd_config, format::CanMessageType::CanFd, kDefaultFdPoolName);
    if (!effective_fd_config_ret) {
        return etl::unexpected(effective_fd_config_ret.error());
    }
    auto effective_xl_config_ret =
        ValidateAndNormalizePoolConfig(xl_config, format::CanMessageType::CanXl, kDefaultXlPoolName);
    if (!effective_xl_config_ret) {
        return etl::unexpected(effective_xl_config_ret.error());
    }

    bool made_progress = false;
    bool has_error = false;
    bool has_transitioning = false;
    LifecycleError first_error = LifecycleErrorType::InvalidState;
    auto record_error = [&](LifecycleError error) {
        if (!has_error) {
            has_error = true;
            first_error = error;
        }
    };

    {
        const LifecycleState state = cc_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Configuring) {
            has_transitioning = true;
        } else if (state == LifecycleState::Unconfigured) {
            auto ret = cc_pool_.Configure(*effective_cc_config_ret);
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = fd_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Configuring) {
            has_transitioning = true;
        } else if (state == LifecycleState::Unconfigured) {
            auto ret = fd_pool_.Configure(*effective_fd_config_ret);
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = xl_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Configuring) {
            has_transitioning = true;
        } else if (state == LifecycleState::Unconfigured) {
            auto ret = xl_pool_.Configure(*effective_xl_config_ret);
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }

    if (GetState() == LifecycleState::Configured) {
        return {};
    }
    if (has_transitioning || made_progress) {
        return etl::unexpected(LifecycleErrorType::PartialStateTransition);
    }
    if (has_error) {
        return etl::unexpected(first_error);
    }
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<FramePoolConfig, LifecycleError> FrameMessageAllocator::ValidateAndNormalizePoolConfig(
    const FramePoolConfig& config, format::CanMessageType frame_type, const char* default_name) const {
    FramePoolConfig effective_config = config;
    const size_t type_index = static_cast<size_t>(frame_type);

    const size_t min_frame_buffer_size = format::MAX_CAN_MESSAGE_FRAME_SIZE_BY_TYPE[type_index];
    const size_t max_message_count = BROKER_FRAME_POOL_MAX_FRAMES_BY_CAN_MESSAGE_TYPE[type_index];
    const bool is_type_enabled =
        (frame_type == format::CanMessageType::CanCc) ||
        ((frame_type == format::CanMessageType::CanFd) && BROKER_CAN_FD_ENABLED) ||
        ((frame_type == format::CanMessageType::CanXl) && BROKER_CAN_XL_ENABLED);

    if (effective_config.name == nullptr) {
        effective_config.name = default_name;
    }

    if (effective_config.frame_buffer_size < min_frame_buffer_size) {
        return etl::unexpected(LifecycleErrorType::InvalidConfiguration);
    }

    if (!is_type_enabled) {
        if (effective_config.message_count != 0U) {
            return etl::unexpected(LifecycleErrorType::InvalidConfiguration);
        }
        return effective_config;
    }

    if (BROKER_FRAME_POOL_SIZE_CONFIGURABLE) {
        if (effective_config.message_count > max_message_count) {
            return etl::unexpected(LifecycleErrorType::InvalidConfiguration);
        }
    } else {
        if (effective_config.message_count != max_message_count) {
            return etl::unexpected(LifecycleErrorType::InvalidConfiguration);
        }
    }

    return effective_config;
}

etl::expected<void, LifecycleError> FrameMessageAllocator::Initialize() {
    bool made_progress = false;
    bool has_error = false;
    bool has_transitioning = false;
    LifecycleError first_error = LifecycleErrorType::InvalidState;
    auto record_error = [&](LifecycleError error) {
        if (!has_error) {
            has_error = true;
            first_error = error;
        }
    };

    {
        const LifecycleState state = cc_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Initializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Configured) {
            auto ret = cc_pool_.Initialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = fd_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Initializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Configured) {
            auto ret = fd_pool_.Initialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = xl_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Initializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Configured) {
            auto ret = xl_pool_.Initialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }

    if (GetState() == LifecycleState::Inactive) {
        return {};
    }
    if (has_transitioning || made_progress) {
        return etl::unexpected(LifecycleErrorType::PartialStateTransition);
    }
    if (has_error) {
        return etl::unexpected(first_error);
    }
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameMessageAllocator::Start() {
    bool made_progress = false;
    bool has_error = false;
    bool has_transitioning = false;
    LifecycleError first_error = LifecycleErrorType::InvalidState;
    auto record_error = [&](LifecycleError error) {
        if (!has_error) {
            has_error = true;
            first_error = error;
        }
    };

    {
        const LifecycleState state = cc_pool_.GetState();
        if (state == LifecycleState::Active) {
        } else if (state == LifecycleState::Starting) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = cc_pool_.Start();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = fd_pool_.GetState();
        if (state == LifecycleState::Active) {
        } else if (state == LifecycleState::Starting) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = fd_pool_.Start();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = xl_pool_.GetState();
        if (state == LifecycleState::Active) {
        } else if (state == LifecycleState::Starting) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = xl_pool_.Start();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }

    if (GetState() == LifecycleState::Active) {
        return {};
    }
    if (has_transitioning || made_progress) {
        return etl::unexpected(LifecycleErrorType::PartialStateTransition);
    }
    if (has_error) {
        return etl::unexpected(first_error);
    }
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameMessageAllocator::Stop() {
    bool made_progress = false;
    bool has_error = false;
    bool has_transitioning = false;
    LifecycleError first_error = LifecycleErrorType::InvalidState;
    auto record_error = [&](LifecycleError error) {
        if (!has_error) {
            has_error = true;
            first_error = error;
        }
    };

    {
        const LifecycleState state = cc_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Stopping) {
            has_transitioning = true;
        } else if (state == LifecycleState::Active) {
            auto ret = cc_pool_.Stop();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = fd_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Stopping) {
            has_transitioning = true;
        } else if (state == LifecycleState::Active) {
            auto ret = fd_pool_.Stop();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = xl_pool_.GetState();
        if (state == LifecycleState::Inactive) {
        } else if (state == LifecycleState::Stopping) {
            has_transitioning = true;
        } else if (state == LifecycleState::Active) {
            auto ret = xl_pool_.Stop();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }

    if (GetState() == LifecycleState::Inactive) {
        return {};
    }
    if (has_transitioning || made_progress) {
        return etl::unexpected(LifecycleErrorType::PartialStateTransition);
    }
    if (has_error) {
        return etl::unexpected(first_error);
    }
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameMessageAllocator::Deinitialize() {
    bool made_progress = false;
    bool has_error = false;
    bool has_transitioning = false;
    LifecycleError first_error = LifecycleErrorType::InvalidState;
    auto record_error = [&](LifecycleError error) {
        if (!has_error) {
            has_error = true;
            first_error = error;
        }
    };

    {
        const LifecycleState state = cc_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Deinitializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = cc_pool_.Deinitialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = fd_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Deinitializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = fd_pool_.Deinitialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }
    {
        const LifecycleState state = xl_pool_.GetState();
        if (state == LifecycleState::Configured) {
        } else if (state == LifecycleState::Deinitializing) {
            has_transitioning = true;
        } else if (state == LifecycleState::Inactive) {
            auto ret = xl_pool_.Deinitialize();
            if (ret) {
                made_progress = true;
            } else {
                record_error(ret.error());
            }
        } else {
            record_error(LifecycleErrorType::InvalidState);
        }
    }

    if (GetState() == LifecycleState::Configured) {
        return {};
    }
    if (has_transitioning || made_progress) {
        return etl::unexpected(LifecycleErrorType::PartialStateTransition);
    }
    if (has_error) {
        return etl::unexpected(first_error);
    }
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

LifecycleState FrameMessageAllocator::GetState() const {
    return CombinePoolStates(cc_pool_.GetState(), fd_pool_.GetState(), xl_pool_.GetState());
}

etl::expected<FrameMessage*, FramePoolError> FrameMessageAllocator::AllocateFrameMessage(
    size_t required_payload_bytes, message::MessageType message_type,
    const publisher::FramePublisherHandle_t* publisher_handle, uint32_t timeout_ticks) {
    FramePool* pool = SelectPoolForPayloadSize(required_payload_bytes);
    if (pool == nullptr) {
        return etl::unexpected(FramePoolError::UnsupportedFrameSize);
    }
    return pool->AllocateFrameMessage(message_type, publisher_handle, timeout_ticks);
}

etl::expected<FrameMessage*, FramePoolError> FrameMessageAllocator::AllocateFrameMessage(
    format::CanMessageType can_message_type, message::MessageType message_type,
    const publisher::FramePublisherHandle_t* publisher_handle, uint32_t timeout_ticks) {
    FramePool* pool = SelectPoolForCanMessageType(can_message_type);
    if (pool == nullptr) {
        return etl::unexpected(FramePoolError::UnsupportedFrameSize);
    }
    return pool->AllocateFrameMessage(message_type, publisher_handle, timeout_ticks);
}

LifecycleState FrameMessageAllocator::CombinePoolStates(
    LifecycleState cc_state, LifecycleState fd_state, LifecycleState xl_state) const {
    if (fd_state != cc_state) {
        return LifecycleState::Mixed;
    }
    if (xl_state != cc_state) {
        return LifecycleState::Mixed;
    }
    return cc_state;
}

FramePool* FrameMessageAllocator::SelectPoolForPayloadSize(size_t required_payload_bytes) {
    if (required_payload_bytes <= format::MAX_CC_PAYLOAD_SIZE) {
        return &cc_pool_;
    }
    if (BROKER_CAN_FD_ENABLED && (required_payload_bytes <= format::MAX_FD_PAYLOAD_SIZE)) {
        return &fd_pool_;
    }
    if (BROKER_CAN_XL_ENABLED && (required_payload_bytes <= format::MAX_XL_PAYLOAD_SIZE)) {
        return &xl_pool_;
    }
    return nullptr;
}

FramePool* FrameMessageAllocator::SelectPoolForCanMessageType(format::CanMessageType can_message_type) {
    switch (can_message_type) {
        case format::CanMessageType::CanCc:
            return &cc_pool_;
        case format::CanMessageType::CanFd:
            return BROKER_CAN_FD_ENABLED ? &fd_pool_ : nullptr;
        case format::CanMessageType::CanXl:
            return BROKER_CAN_XL_ENABLED ? &xl_pool_ : nullptr;
        default:
            return nullptr;
    }
}

}  // namespace spiopen::broker
