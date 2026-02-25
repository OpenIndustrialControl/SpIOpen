/*
SpIOpen Frame Description

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once

namespace spiopen {

    class Frame final {
        public:
            /* Structure that contains the flags for a SpIOpen frame */
            typedef struct {
                unsigned int RTR : 1; // Remote Transmission Request/Remote Request Substitution flag
                unsigned int BRS : 1; // Bit Rate Switch flag
                unsigned int ESI : 1; // Error Status Indicator flag
                unsigned int IDE : 1; // Identifier Extension flag
                unsigned int FDF : 1; // Flexible Data-Rate Format flag
                unsigned int XLF : 1; // XL Format flag
                unsigned int TTL : 1; // Time to Live flag
                unsigned int WA : 1; // Word Alignment flag
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
            * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no ownership of the pointer.
            * @param payload_length decoded payload length in bytes
            */  
            Frame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, XLControl xl_control, uint8_t *payload_data, size_t payload_length) : 
                can_identifier(can_identifier), can_flags(can_flags), time_to_live(time_to_live), xl_control(xl_control), payload_data(payload_data), payload_length(payload_length) {}
            #else
            /**
            * @brief Constructor for the Frame class
            * @param can_identifier 11 or 29 bit CAN identifier
            * @param can_flags CAN flags
            * @param time_to_live Time to Live counter, only populated if TTL flag is set
            * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no ownership of the pointer.
            * @param payload_length decoded payload length in bytes
            */  
            Frame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, uint8_t *payload_data, size_t payload_length) : 
                can_identifier(can_identifier), can_flags(can_flags), time_to_live(time_to_live), payload_data(payload_data), payload_length(payload_length) {}
            #endif
            ~Frame();

        /* Getters for the Frame class fields*/
        public:
            uint32_t GetCanIdentifier() const { return can_identifier; }
            uint8_t GetTimeToLive() const { return time_to_live; }
            #ifdef CONFIG_SPIOPEN_SUPPORT_CAN_XL
            XLControl GetXlControl() const { return xl_control; }
            #endif
            uint8_t *GetPayloadData() const { return payload_data; }
            size_t GetPayloadLength() const { return payload_length; }
        /* Getters for the CAN flags*/
        public:
            bool GetFlagRTR() const { return can_flags.RTR; }
            bool GetFlagBRS() const { return can_flags.BRS; }
            bool GetFlagESI() const { return can_flags.ESI; }
            bool GetFlagIDE() const { return can_flags.IDE; }
            bool GetFlagFDF() const { return can_flags.FDF; }
            bool GetFlagXLF() const { return can_flags.XLF; }
            bool GetFlagTTL() const { return can_flags.TTL; }
            bool GetFlagWA() const { return can_flags.WA; }

        /* Get other constant information about the Frame */
        public:
            /**
            * @brief Calculate the length of the SpIOpen frame, from start of preamble to end of CRC, including padding
            * @return The length of the frame in bytes
            */
            size_t GetFrameLength() const;

        /* Helper Functions that modify the Frame class*/
        public:
            #ifdef CONFIG_SPIOPEN_SUPPORT_CAN_XL
            /**
            * @brief Sets all internal fields based on the provided inputs
            * @param can_identifier 11 or 29 bit CAN identifier
            * @param can_flags CAN flags
            * @param time_to_live Time to Live counter, only populated if TTL flag is set
            * @param xl_control XL control fields, only populated if XLF flag is set
            * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no ownership of the pointer.
            * @param payload_length decoded payload length in bytes
            */
            void SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, XLControl xl_control, uint8_t *payload_data, size_t payload_length);
            #else
            /**
            * @brief Sets all internal fields based on the provided inputs
            * @param can_identifier 11 or 29 bit CAN identifier
            * @param can_flags CAN flags
            * @param time_to_live Time to Live counter, only populated if TTL flag is set
            * @param payload_data Pointer to the payload data, only populated if payload length is non-zero. Structure has no ownership of the pointer.
            * @param payload_length decoded payload length in bytes
            */
            void SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, uint8_t *payload_data, size_t payload_length);
            #endif
            
            /**
            * @brief Handles decrementing the Time to Live counter if the TTL flag is set
            * @return True if the counter is decremented and is now 0, false if TTL flag is not set or counter is above 0
            */
            bool DecrementAndCheckTimeToLive();



        /* Fields that represent the individual elements of the SpIOpen frame*/
        private:
            uint32_t can_identifier; // 11 or 29 bit CAN identifier
            Flags can_flags;
            uint8_t time_to_live; // Time to Live counter, only populated if TTL flag is set
            #ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
            XLControl xl_control; // XL control fields, only populated if XLF flag is set
            #endif
            uint8_t *payload_data; // Pointer to the payload data, only populated if payload length is non-zero. Structure has no ownership of the pointer.
            size_t payload_length; // decoded payload length in bytes
    }


}
