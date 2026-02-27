/*
SpIOpen Frame Description

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>

#include "spiopen_frame_format.h"

namespace spiopen {

class Frame final {
   public:
    /* Structure that contains the flags for a SpIOpen frame */
    typedef struct {
        unsigned int RTR : 1;  // Remote Transmission Request/Remote Request Substitution flag
        unsigned int BRS : 1;  // Bit Rate Switch flag
        unsigned int ESI : 1;  // Error Status Indicator flag
        unsigned int IDE : 1;  // Identifier Extension flag
        unsigned int FDF : 1;  // Flexible Data-Rate Format flag
        unsigned int XLF : 1;  // XL Format flag
        unsigned int TTL : 1;  // Time to Live flag
        unsigned int WA : 1;   // Word Alignment flag
    } Flags;

    /* Structure that contains the CAN-XL control fields*/
    typedef struct {
        uint8_t payload_type;
        uint8_t virtual_can_network_id;
        uint32_t addressing_field;
    } XLControl;

    /* Fields that represent the individual elements of the SpIOpen frame*/
   public:
    uint32_t can_identifier;  // 11 or 29 bit CAN identifier
    Flags can_flags;
    uint8_t time_to_live;  // Time to Live counter, only populated if TTL flag is set
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    XLControl xl_control;  // XL control fields, only populated if XLF flag is set
#endif
    uint8_t *payload_data;  // Pointer to the payload data, only populated if payload length is non-zero. Structure has
                            // no ownership of the pointer.
    size_t payload_length;  // decoded payload length in bytes

    /* Constructor and destructor for the Frame class*/
    inline Frame()
        : can_identifier(0U),
          can_flags({}),
          time_to_live(0U),
          xl_control({}),
          payload_data(nullptr),
          payload_length(0U) {}
    inline ~Frame() = default;

    /* Get other information about the Frame that can be calcualted from the fields*/ public:
    /**
     * @brief Calculate the length of the header (not including preamble), from
     *        the format header until right before the payload.
     * @return The length of the header in bytes
     */
    inline size_t GetHeaderLength() const {
        size_t header_length = format::FORMAT_HEADER_SIZE + format::CAN_IDENTIFIER_SIZE;
        if (can_flags.IDE) {
            header_length += format::CAN_IDENTIFIER_EXTENSION_SIZE;
        }

        if (can_flags.TTL) {
            header_length += format::TIME_TO_LIVE_SIZE;
        }
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
        if (can_flags.XLF) {
            header_length += format::XL_CONTROL_SIZE;
        }
#endif
        return header_length;
    }
    /**
     * @brief Calculate the length of the SpIOpen frame, from start of preamble to end of CRC, including padding
     * @return The length of the frame in bytes
     */
    inline size_t GetFrameLength() const {
        size_t frame_length = format::PREAMBLE_SIZE + GetHeaderLength() + payload_length;
        frame_length +=
            (payload_length <= format::MAX_CC_PAYLOAD_SIZE) ? format::SHORT_CRC_SIZE : format::LONG_CRC_SIZE;
        if (can_flags.WA && ((frame_length & 0x1U) != 0U)) {
            frame_length += format::MAX_PADDING_SIZE;
        }
        return frame_length;
    }

    /**
     * @brief Clears all frame fields to their default zero/empty state.
     */
    inline void Reset() {
        can_identifier = 0U;
        can_flags = {};
        time_to_live = 0U;
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
        xl_control = {};
#endif
        payload_data = nullptr;
        payload_length = 0U;
    }

    /**
     * @brief Handles decrementing the Time to Live counter if the TTL flag is set
     * @return True if the counter is decremented and is now 0, false if TTL flag is not set or counter is above 0
     */
    inline bool DecrementAndCheckIfTimeToLiveExpired() {
        if (!can_flags.TTL) {
            return false;
        }
        if (time_to_live > 0U) {
            --time_to_live;
        }
        return (time_to_live == 0U);
    }
};

}  // namespace spiopen
