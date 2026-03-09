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
    InvalidStateForPublish, /**< Message not ready to be reinitialized for publish */
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
 * @brief Read-only message interface provided to subscribers.
 *
 * Subscribers must only access frame payload and metadata through this interface.
 * Release is intentionally non-const because it mutates only the reference counter,
 * not the frame payload.
 */
class IReadOnlyFrameMessage {
   public:
    virtual ~IReadOnlyFrameMessage() = default;

    /**
     * @brief Gets the immutable frame payload metadata.
     * @return Immutable reference to the frame
     */
    virtual const Frame& GetFrame() const = 0;

    /**
     * @brief Gets the immutable frame buffer wrapper.
     * @return Immutable reference to the frame buffer
     */
    virtual const FrameBuffer& GetFrameBuffer() const = 0;

    /**
     * @brief Gets the message type mask used by broker filtering.
     * @return Message type flags associated with this message
     */
    virtual message::MessageType GetMessageType() const = 0;

    /**
     * @brief Gets publisher descriptor associated with this message.
     * @return Publisher handle pointer captured at publish initialization (or nullptr)
     */
    virtual const publisher::FramePublisherHandle_t* GetPublisherHandle() const = 0;

    /**
     * @brief Releases one reference to this message.
     *
     * This path is designed to be infallible in normal operation. If release reaches
     * an impossible state (invalid pool pointer, queue invariant violation, etc), the
     * implementation is expected to hard-fault/abort rather than returning an error.
     */
    virtual void Release() = 0;
};

/**
 * @brief Concrete reference-counted message allocated by FramePool.
 *
 * A FrameMessage owns the buffer memory used by its internal FrameBuffer and tracks
 * references from publisher, broker, and subscribers. It also contains a pointer back
 * to the owning FramePool so it can be returned automatically when the reference
 * counter reaches zero.
 */
class FrameMessage final : public IReadOnlyFrameMessage {
   public:
    /**
     * @brief Constructs a new frame message with pool ownership and internal buffer storage.
     * @param owning_pool Pointer to pool that owns and recycles this message
     * @param frame_storage Mutable byte storage used by internal FrameBuffer
     */
    FrameMessage(FramePool* owning_pool, etl::span<uint8_t> frame_storage);

    ~FrameMessage() = default;

    /**
     * @brief Initializes message for publisher use with source metadata and reference count.
     * @param message_type Message type flags for broker fan-out filtering
     * @param publisher_handle Optional non-owning pointer to publisher descriptor associated with this message
     * @param initial_references Initial reference counter value (typically 1 for publisher)
     * @return Success if initialization succeeded, error on invalid state/value
     */
    etl::expected<void, FrameMessageError> InitializeForPublish(
        , message::MessageType message_type, const publisher::FramePublisherHandle_t* publisher_handle = nullptr,
        uint8_t initial_references = 1U);

    /**
     * @brief Acquires one additional reference to this message.
     * @return Success if counter incremented, error on overflow/state
     */
    etl::expected<void, FrameMessageError> AcquireReference();

    /**
     * @brief Gets mutable access to frame for publisher-side construction before publish.
     * @return Mutable frame reference
     */
    Frame& GetMutableFrame();

    /**
     * @brief Gets mutable access to frame buffer for publisher-side serialization/update.
     * @return Mutable frame buffer reference
     */
    FrameBuffer& GetMutableFrameBuffer();

    /**
     * @brief Gets immutable frame payload metadata.
     * @return Immutable reference to frame
     */
    const Frame& GetFrame() const override;

    /**
     * @brief Gets immutable frame buffer wrapper.
     * @return Immutable reference to frame buffer
     */
    const FrameBuffer& GetFrameBuffer() const override;

    /**
     * @brief Gets message type flags.
     * @return Message type flags
     */
    message::MessageType GetMessageType() const override;

    /**
     * @brief Gets publisher descriptor associated with this message.
     * @return Publisher handle pointer captured at publish initialization (or nullptr)
     */
    const publisher::FramePublisherHandle_t* GetPublisherHandle() const override;

    /**
     * @brief Gets current reference count value.
     * @return Current reference count snapshot
     */
    uint8_t GetReferenceCount() const;

    /**
     * @brief Releases one reference and requeues to pool when count reaches zero.
     *
     * This function intentionally does not return errors. If an impossible edge case
     * is detected (e.g. uninitialized pool, invalid ownership), implementation should
     * hard-fault/abort.
     */
    void Release() override;

    /**
     * @brief Gets owning frame pool pointer.
     * @return Owning pool pointer
     */
    FramePool* GetOwningPool() const;

   private:
    FramePool* owning_pool_;
    FrameBuffer frame_buffer_;
    std::atomic<uint8_t> reference_count_;
    message::MessageType message_type_;
    const publisher::FramePublisherHandle_t* publisher_handle_;
};

}  // namespace spiopen::broker