/*
SpIOpen Broker Frame Message : Used to pass frames between publishers and subscribers within the broker.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "etl/expected.h"
#include "etl/span.h"
#include "spiopen_frame_buffer.h"

namespace spiopen::broker {

class FramePool;
namespace publisher {
struct FramePublisherHandle_t;
}

namespace message {

/**
 * @brief Flag-based message types used for subscriber filtering in the broker.
 *
 * Underlying type is uint16_t. For bitwise combination (e.g. multiple sources) or
 * mask testing, use etl::to_underlying() or static_cast to uint16_t at call sites;
 * the standard and ETL do not provide enum-flag operators, so no helpers are defined here.
 */
enum class MessageType : uint16_t {
    None = 0x0000U,          /**< No message type flags set */
    MasterToSlave = 0x0001U, /**< Message received from MOSI drop bus or equivalent ingress source */
    SlaveToMaster = 0x0002U, /**< Message intended for MISO chain egress or equivalent upstream path */
};

}  // namespace message

/**
 * @brief Error codes returned by FrameMessage reference count operations.
 */
enum class FrameMessageError : uint8_t {
    InvalidPool = 1,        /**< Message has no owning pool reference */
    ReferenceOverflow,      /**< Reference increment would overflow counter */
    ReferenceUnderflow,     /**< Reference decrement called at zero */
    InvalidStateForPublish, /**< Message not ready for allocate/publish state transition */
    NotMutable,             /**< Message state is not Allocated; mutable access denied */
};

#ifdef CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT
#if (CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT > 255)
#error "CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT must fit in uint8_t (<= 255). Adjust Kconfig range or refcount type."
#endif
static constexpr size_t BROKER_MAX_SUBSCRIBER_COUNT = CONFIG_SPIOPEN_BROKER_MAX_SUBSCRIBER_COUNT;
#else
static constexpr size_t BROKER_MAX_SUBSCRIBER_COUNT = 8U;
#endif

/**
 * @brief Runtime state of a FrameMessage lifecycle.
 */
enum class FrameMessageState : uint8_t {
    Available = 0, /**< Message is in pool and available for allocation */
    Allocated,     /**< Message is owned by publisher and mutable */
    Published,     /**< Message has been published and must be treated as immutable */
};

/**
 * @brief Concrete reference-counted message allocated by FramePool.
 *
 * A FrameMessage owns the buffer memory used by its internal FrameBuffer and tracks
 * references from publisher, broker, and subscribers. It also contains a pointer back
 * to the owning FramePool so it can be returned automatically when the reference
 * counter reaches zero.
 */
class FrameMessage final {
   public:
    /**
     * @brief Constructs a new frame message with pool ownership and internal buffer storage.
     * @param owning_pool Pool that owns and recycles this message (must outlive the message)
     * @param frame_storage Mutable byte storage used by internal FrameBuffer
     */
    FrameMessage(FramePool& owning_pool, etl::span<uint8_t> frame_storage);

    ~FrameMessage() = default;

    /**
     * @brief Allocates this message to a publisher and transitions state Available -> Allocated.
     *
     * This function is the allocation transition gate for publisher ownership. It
     * sets publisher/message metadata and initial reference count only when current
     * state is Available.
     *
     * @param message_type Message type flags for broker fan-out filtering
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param initial_references Initial reference counter value (typically 1 for publisher)
     * @note ISR-safe.
     * @return Success on transition to Allocated; error when state/value is invalid
     */
    etl::expected<void, FrameMessageError> AllocateToPublisher(
        message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint8_t initial_references = 1U);

    /**
     * @brief Locks message for publish and transitions state Allocated -> Published.
     *
     * After this transition, mutable accessors should reject writes and callers
     * should treat the payload as immutable.
     *
     * @note ISR-safe.
     * @return Success on transition to Published; error when current state is not Allocated
     */
    etl::expected<void, FrameMessageError> LockForPublishing();

    /**
     * @brief Acquires one additional reference to this message.
     * @note ISR-safe.
     * @return Success if counter incremented, error on overflow/state
     */
    etl::expected<void, FrameMessageError> AcquireReference();

    /**
     * @brief Gets mutable access to frame for publisher-side construction before publish.
     *
     * Succeeds only when message state is Allocated; otherwise returns NotMutable.
     *
     * @return Pointer to mutable frame on success; error when state is not Allocated
     */
    etl::expected<Frame*, FrameMessageError> GetMutableFrame();

    /**
     * @brief Gets mutable access to frame buffer for publisher-side serialization/update.
     *
     * Succeeds only when message state is Allocated; otherwise returns NotMutable.
     *
     * @return Pointer to mutable frame buffer on success; error when state is not Allocated
     */
    etl::expected<FrameBuffer*, FrameMessageError> GetMutableFrameBuffer();

    /**
     * @brief Gets immutable frame payload metadata.
     * @return Immutable reference to frame
     */
    const Frame& GetFrame() const;

    /**
     * @brief Gets immutable frame buffer wrapper.
     * @return Immutable reference to frame buffer
     */
    const FrameBuffer& GetFrameBuffer() const;

    /**
     * @brief Gets message type flags.
     * @return Message type flags
     */
    message::MessageType GetMessageType() const;

    /**
     * @brief Gets publisher descriptor associated with this message.
     * @return Publisher handle pointer captured at publish initialization (or nullptr)
     */
    const publisher::FramePublisherHandle_t* GetPublisherHandle() const;

    /**
     * @brief Gets current message lifecycle state.
     * @return Current FrameMessageState
     */
    FrameMessageState GetState() const;

    /**
     * @brief Gets current reference count value.
     * @return Current reference count snapshot
     */
    uint8_t GetReferenceCount() const;

    /**
     * @brief Releases one reference and requeues to pool when count reaches zero.
     *
     * This function intentionally does not return errors and is valid to call from
     * any message state. Depending on reference-count ownership flow, release may
     * transition the message toward Allocated ownership or, on final release, return
     * it to Available via pool requeue.
     *
     * If an impossible edge case is detected (e.g. uninitialized pool, invalid
     * ownership), implementation should hard-fault/abort.
     *
     * @note ISR-safe.
     */
    void Release();

    /**
     * @brief Gets owning frame pool reference.
     * @return Owning pool reference
     */
    FramePool& GetOwningPool() const;

   private:
    bool IsState(FrameMessageState expected,
                 std::memory_order order = std::memory_order_relaxed) const {
        return state_.load(order) == expected;
    }

    FramePool& owning_pool_;
    FrameBuffer frame_buffer_;
    std::atomic<uint8_t> reference_count_;
    std::atomic<FrameMessageState> state_;
    message::MessageType message_type_;
    const publisher::FramePublisherHandle_t* publisher_handle_;
};

}  // namespace spiopen::broker