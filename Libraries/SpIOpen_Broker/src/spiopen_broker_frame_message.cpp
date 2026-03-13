/*
SpIOpen Broker Frame Message : Stub implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_broker_frame_message.h"

#include <limits>

#include "spiopen_broker_frame_pool.h"
#include "spiopen_broker_lifecycle.h"

namespace spiopen::broker {

FrameMessage::FrameMessage(FramePool& owning_pool, etl::span<uint8_t> frame_storage)
    : owning_pool_(owning_pool),
      frame_buffer_(frame_storage),
      reference_count_(0U),
      state_(FrameMessageState::Available),
      message_type_(message::MessageType::None),
      publisher_handle_(nullptr) {}

etl::expected<void, FrameMessageError> FrameMessage::AllocateToPublisher(
    message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle,
    uint8_t initial_references) {
    if (owning_pool_.GetState() != LifecycleState::Active) {
        return etl::unexpected(FrameMessageError::InvalidPool);
    }
    if (initial_references == 0U) {
        return etl::unexpected(FrameMessageError::ReferenceUnderflow);
    }

    FrameMessageState expected_state = FrameMessageState::Available;
    if (!state_.compare_exchange_strong(expected_state, FrameMessageState::Allocated, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
        return etl::unexpected(FrameMessageError::InvalidStateForPublish);
    }

    message_type_ = message_type;
    publisher_handle_ = publisher_handle;
    reference_count_.store(initial_references, std::memory_order_release);
    return {};
}

etl::expected<void, FrameMessageError> FrameMessage::LockForPublishing() {
    FrameMessageState expected_state = FrameMessageState::Allocated;
    if (!state_.compare_exchange_strong(expected_state, FrameMessageState::Published, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
        return etl::unexpected(FrameMessageError::InvalidStateForPublish);
    }
    return {};
}

etl::expected<void, FrameMessageError> FrameMessage::AcquireReference() {
    if (IsState(FrameMessageState::Available)) {
        return etl::unexpected(FrameMessageError::InvalidStateForPublish);
    }

    uint8_t current_count = reference_count_.load(std::memory_order_relaxed);
    while (true) {
        if (current_count == std::numeric_limits<uint8_t>::max()) {
            return etl::unexpected(FrameMessageError::ReferenceOverflow);
        }

        if (reference_count_.compare_exchange_weak(current_count, static_cast<uint8_t>(current_count + 1U),
                                                   std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return {};
        }
    }
}

etl::expected<Frame*, FrameMessageError> FrameMessage::GetMutableFrame() {
    if (!IsState(FrameMessageState::Allocated)) {
        return etl::unexpected(FrameMessageError::NotMutable);
    }
    return &frame_buffer_.GetFrame();
}

etl::expected<FrameBuffer*, FrameMessageError> FrameMessage::GetMutableFrameBuffer() {
    if (!IsState(FrameMessageState::Allocated)) {
        return etl::unexpected(FrameMessageError::NotMutable);
    }
    return &frame_buffer_;
}

const Frame& FrameMessage::GetFrame() const { return const_cast<FrameBuffer&>(frame_buffer_).GetFrame(); }

const FrameBuffer& FrameMessage::GetFrameBuffer() const { return frame_buffer_; }

message::MessageType FrameMessage::GetMessageType() const { return message_type_; }

const publisher::FramePublisherHandle_t* FrameMessage::GetPublisherHandle() const { return publisher_handle_; }

FrameMessageState FrameMessage::GetState() const { return state_.load(std::memory_order_relaxed); }

uint8_t FrameMessage::GetReferenceCount() const { return reference_count_.load(std::memory_order_relaxed); }

void FrameMessage::Release() {
    uint8_t current_count = reference_count_.load(std::memory_order_relaxed);
    // this loop basically tried to decrement the atomic by one, exiting if the count goes below zero through some other
    // concurrent code, or if the decrement succeeds (and the message is requeued)
    while (true) {
        if (current_count == 0U) {
            return;
        }

        const uint8_t next_count = static_cast<uint8_t>(current_count - 1U);
        if (reference_count_.compare_exchange_weak(current_count, next_count, std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
            if (next_count == 0U) {
                message_type_ = message::MessageType::None;
                publisher_handle_ = nullptr;
                state_.store(FrameMessageState::Available, std::memory_order_release);
                owning_pool_.RequeueFrameMessage(this);
            }
            return;
        }
    }
}

FramePool& FrameMessage::GetOwningPool() const { return owning_pool_; }

}  // namespace spiopen::broker
