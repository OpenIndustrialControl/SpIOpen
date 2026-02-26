/*
SpIOpen Frame Implementation

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/

#include "spiopen_frame.h"

#include <cstddef>
#include <cstdint>

#include "spiopen_frame_format.h"

namespace spiopen {

Frame::Frame()
    : can_identifier(0U),
      can_flags{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
      time_to_live(0U),
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
      xl_control{0U, 0U, 0U},
#endif
      payload_data(nullptr),
      payload_length(0U) {
}

Frame::~Frame() = default;

void Frame::Reset() {
    can_identifier = 0U;
    can_flags = {};
    time_to_live = 0U;
#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
    xl_control = {};
#endif
    payload_data = nullptr;
    payload_length = 0U;
}

#ifdef CONFIG_SPIOPEN_FRAME_CAN_XL_ENABLE
void Frame::SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, XLControl xl_control,
                     uint8_t *payload_data, size_t payload_length) {
    this->can_identifier = can_identifier;
    this->can_flags = can_flags;
    this->time_to_live = time_to_live;
    this->xl_control = xl_control;
    this->payload_data = payload_data;
    this->payload_length = payload_length;
}
#else
void Frame::SetFrame(uint32_t can_identifier, Flags can_flags, uint8_t time_to_live, uint8_t *payload_data,
                     size_t payload_length) {
    this->can_identifier = can_identifier;
    this->can_flags = can_flags;
    this->time_to_live = time_to_live;
    this->payload_data = payload_data;
    this->payload_length = payload_length;
}
#endif

bool Frame::DecrementAndCheckIfTimeToLiveExpired() {
    if (!can_flags.TTL) {
        return false;
    }
    if (time_to_live > 0U) {
        --time_to_live;
    }
    return (time_to_live == 0U);
}

}  // namespace spiopen
