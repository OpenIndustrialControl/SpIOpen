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

    /* Constructor and destructor for the Frame class*/
   public:
    Frame();
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    /**
     * @brief Constructor for the Frame class
     * @param can_identifier 11 or 29 bit CAN identifier
     * @param can_flags CAN flags
     * @param time_to_live Time to Live counter, only populated if TTL flag is set
     * @param xl_control XL control fields, only populated if XLF flag is set
     * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no
     * ownership of the pointer.
     * @param payload_length decoded payload length in bytes
     */
    Frame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, XLControl xl_control, uint8_t *payload_data,
          size_t payload_length)
        : can_identifier(can_identifier),
          can_flags(can_flags),
          time_to_live(time_to_live),
          xl_control(xl_control),
          payload_data(payload_data),
          payload_length(payload_length) {}
#else
    /**
     * @brief Constructor for the Frame class
     * @param can_identifier 11 or 29 bit CAN identifier
     * @param can_flags CAN flags
     * @param time_to_live Time to Live counter, only populated if TTL flag is set
     * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no
     * ownership of the pointer.
     * @param payload_length decoded payload length in bytes
     */
    Frame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, uint8_t *payload_data, size_t payload_length)
        : can_identifier(can_identifier),
          can_flags(can_flags),
          time_to_live(time_to_live),
          payload_data(payload_data),
          payload_length(payload_length) {}
#endif
    ~Frame();

    /* Getters for the Frame class fields (inline for efficiency) */
    // public:
    //     inline uint32_t GetCanIdentifier() const { return can_identifier; }
    //     inline uint8_t GetTimeToLive() const { return time_to_live; }
    //     #ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    //     inline XLControl GetXlControl() const { return xl_control; }
    //     #endif
    //     inline uint8_t *GetPayloadData() const { return payload_data; }
    //     inline size_t GetPayloadLength() const { return payload_length; }
    /* Getters for the CAN flags (inline for efficiency) */
   public:
    inline bool GetFlagRTR() const { return can_flags.RTR; }
    inline bool GetFlagBRS() const { return can_flags.BRS; }
    inline bool GetFlagESI() const { return can_flags.ESI; }
    inline bool GetFlagIDE() const { return can_flags.IDE; }
    inline bool GetFlagFDF() const { return can_flags.FDF; }
    inline bool GetFlagXLF() const { return can_flags.XLF; }
    inline bool GetFlagTTL() const { return can_flags.TTL; }
    inline bool GetFlagWA() const { return can_flags.WA; }

    /* Get other constant information about the Frame */
   public:
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

    /* Helper Functions that modify the Frame class*/
   public:
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    /**
     * @brief Sets all internal fields based on the provided inputs
     * @param can_identifier 11 or 29 bit CAN identifier
     * @param can_flags CAN flags
     * @param time_to_live Time to Live counter, only populated if TTL flag is set
     * @param xl_control XL control fields, only populated if XLF flag is set
     * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no
     * ownership of the pointer.
     * @param payload_length decoded payload length in bytes
     */
    void SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, XLControl xl_control,
                  uint8_t *payload_data, size_t payload_length);
#else
    /**
     * @brief Sets all internal fields based on the provided inputs
     * @param can_identifier 11 or 29 bit CAN identifier
     * @param can_flags CAN flags
     * @param time_to_live Time to Live counter, only populated if TTL flag is set
     * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no
     * ownership of the pointer.
     * @param payload_length decoded payload length in bytes
     */
    void SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, uint8_t *payload_data,
                  size_t payload_length);
#endif

    /**
     * @brief Clears all frame fields to their default zero/empty state.
     */
    void Reset();

    /**
     * @brief Handles decrementing the Time to Live counter if the TTL flag is set
     * @return True if the counter is decremented and is now 0, false if TTL flag is not set or counter is above 0
     */
    bool DecrementAndCheckIfTimeToLiveExpired();

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
};

}  // namespace spiopen
