/*
SpIOpen Broker Frame Broker : Stub implementation.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_broker_frame_broker.h"

namespace spiopen::broker {

FrameBroker::FrameBroker()
    : config_{nullptr, 0U, 0U, FrameMailboxConfig{0U, etl::span<uint8_t>()}},
      state_(LifecycleState::Unconfigured),
      thread_id_(nullptr),
      inbox_mailbox_(),
      subscribers_(),
      enqueue_error_count_(0U) {
}

etl::expected<void, LifecycleError> FrameBroker::Configure(const FrameBrokerConfig& config) {
    (void)config;
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameBroker::Initialize() {
    return etl::unexpected(LifecycleErrorType::NotConfigured);
}

etl::expected<void, LifecycleError> FrameBroker::Start() {
    return etl::unexpected(LifecycleErrorType::NotInitialized);
}

etl::expected<void, LifecycleError> FrameBroker::Stop() {
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameBroker::Deinitialize() {
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, LifecycleError> FrameBroker::Reset() {
    return etl::unexpected(LifecycleErrorType::InvalidState);
}

etl::expected<void, FrameBrokerError> FrameBroker::Subscribe(subscriber::FrameSubscriberHandle_t* subscriber_handle) {
    (void)subscriber_handle;
    return etl::unexpected(FrameBrokerError::InvalidState);
}

etl::expected<void, FrameBrokerError> FrameBroker::Unsubscribe(
    const subscriber::FrameSubscriberHandle_t* subscriber_handle) {
    (void)subscriber_handle;
    return etl::unexpected(FrameBrokerError::InvalidState);
}

etl::expected<void, FrameBrokerError> FrameBroker::Publish(FrameMessage* message, uint32_t timeout_ticks) {
    (void)message;
    (void)timeout_ticks;
    return etl::unexpected(FrameBrokerError::PublishFailed);
}

LifecycleState FrameBroker::GetState() const {
    return state_.load(std::memory_order_relaxed);
}

uint32_t FrameBroker::GetEnqueueErrorCount() const {
    return enqueue_error_count_.load(std::memory_order_relaxed);
}

void FrameBroker::ThreadEntry(void* context) {
    (void)context;
    // Stub: no broker thread behavior implemented.
}

void FrameBroker::RunLoop() {
    // Stub: no broker routing loop implemented.
}

etl::expected<void, FrameBrokerError> FrameBroker::FanOutToSubscribers(FrameMessage* message) {
    (void)message;
    return etl::unexpected(FrameBrokerError::PublishFailed);
}

}  // namespace spiopen::broker

