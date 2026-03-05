/*
SpIOpen Frame Description

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <cstddef>
#include <cstdint>

#include "etl/span.h"
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
    etl::span<uint8_t> payload;  // Span to the payload data, only populated if payload length is non-zero. This points
                                 // to just the underlying data and does not contain any padding. In the case of API-set
                                 // FD payloads it might be slightly short than the on-the-wire payload pength (see
                                 // @GetPayloadSectionLength() for the on-the-wire length that includes padding).

    /* Constructor and destructor for the Frame class*/
    inline Frame()
        : can_identifier(0U),
          can_flags({}),
          time_to_live(0U),
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
          xl_control({}),
#endif
          payload({}) {
    }
    inline ~Frame() = default;

    inline size_t GetCanIdLength() const {
        return can_flags.IDE ? format::CAN_IDENTIFIER_SIZE + format::CAN_IDENTIFIER_EXTENSION_SIZE
                             : format::CAN_IDENTIFIER_SIZE;
    }

    /* Get other information about the Frame that can be calcualted from the fields*/ public:
    /**
     * @brief Calculate the length of the header (not including preamble), from
     *        the format header until right before the payload.
     * @return The length of the header in bytes
     */
    inline size_t GetHeaderLength() const {
        size_t header_length = format::FORMAT_HEADER_SIZE + GetCanIdLength();
        if (can_flags.TTL) {
            header_length += format::TIME_TO_LIVE_SIZE;
        }
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
        if (can_flags.XLF) {
            header_length += format::XL_CONTROL_SIZE + format::XL_DATA_LENGTH_SIZE;
        }
#endif
        return header_length;
    }

    /**
     * @brief Calculate the length of the payload *on the wire* in bytes, including any padding needed for FDF payloads.
     * @return The length of the payload in bytes
     */
    inline bool TryGetPayloadSectionLength(size_t& payload_length_out) const {
        if (payload.empty()) {
            payload_length_out = 0U;
            return true;
        }

        // CC
        if (!can_flags.XLF && !can_flags.FDF) {
            payload_length_out = payload.size();
            return payload_length_out <= format::MAX_CC_PAYLOAD_SIZE;
        }

        // XL
        if (can_flags.XLF) {
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
            payload_length_out = payload.size();
            return payload_length_out <= format::MAX_XL_PAYLOAD_SIZE;
#else
            return false;
#endif
        }

// FD
#ifdef CONFIG_SPIOPEN_FRAME_CAN_FD_ENABLE
        uint8_t dlc = 0;
        if (!format::TryGetDlcFromPayloadLength(payload.size(), dlc)) {
            return false;
        }
        payload_length_out = format::CAN_FD_PAYLOAD_BY_DLC[dlc & format::HEADER_DLC_MASK];
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Calculate the length of the SpIOpen frame, from start of preamble to end of CRC, including padding
     * @return The length of the frame in bytes
     */
    inline bool TryGetFrameLength(size_t& frame_length_out) const {
        size_t payload_length = 0;
        if (!TryGetPayloadSectionLength(payload_length)) {
            return false;
        }
        size_t crc_length = format::GetCrcLengthFromPayloadLength(payload_length);
        frame_length_out = format::PREAMBLE_SIZE + GetHeaderLength() + payload_length + crc_length;
        if (can_flags.WA && ((frame_length_out & 0x1U) != 0U)) {
            frame_length_out += format::MAX_PADDING_SIZE;
        }
        return true;
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
        payload = {};
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
