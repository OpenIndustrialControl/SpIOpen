/*
SpIOpen Broker Frame Subscriber : Subscriber mailbox and registration handle definitions.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <atomic>
#include <cstdint>

#include "spiopen_broker_frame_mailbox.h"
#include "spiopen_broker_frame_message.h"

namespace spiopen::broker::subscriber {

/**
 * @brief Subscriber registration descriptor used by FrameBroker.
 */
struct FrameSubscriberHandle_t {
    const char* name;                         /**< Debug name for logging/diagnostics */
    FrameMailbox* mailbox;                    /**< Mailbox where broker enqueues matching messages */
    std::atomic<uint16_t> message_type_mask;  /**< Bitmask filter of accepted message::MessageType flags */
    std::atomic<uint32_t> enqueue_error_count; /**< Per-subscriber enqueue failure counter */
};

/**
 * @brief Checks whether subscriber filter accepts provided message type.
 * @param handle Subscriber descriptor
 * @param message_type Incoming message type flags
 * @return True if message should be enqueued to subscriber
 */
inline bool AcceptsMessageType(const FrameSubscriberHandle_t& handle, message::MessageType message_type) {
    const uint16_t filter = handle.message_type_mask.load(std::memory_order_relaxed);
    const uint16_t incoming = static_cast<uint16_t>(message_type);
    return (filter & incoming) != 0U;
}

}  // namespace spiopen::broker::subscriber

