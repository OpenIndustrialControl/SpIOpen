/*
SpIOpen Frame Algorithm Implementation (Default / Software)

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0

Default pure-software implementation. Replace this translation unit at link
time with a platform-specific .cpp for hardware-accelerated CRC/SECDED
(see AlgorithmBackend.md).
*/

#include "spiopen_frame_algorithms.h"

#include "etl/crc16_ccitt.h"
#include "etl/crc32_mpeg2.h"

namespace spiopen::algorithms {

namespace {

static inline uint8_t GetBit(const uint16_t value, const uint8_t one_based_position) {
    return static_cast<uint8_t>((value >> (one_based_position - 1U)) & 0x1U);
}

static inline void SetBit(uint16_t& value, const uint8_t one_based_position, const uint8_t bit) {
    const uint16_t mask = static_cast<uint16_t>(1U << (one_based_position - 1U));
    if (bit != 0U) {
        value = static_cast<uint16_t>(value | mask);
    } else {
        value = static_cast<uint16_t>(value & ~mask);
    }
}

static inline uint8_t Parity16(const uint16_t value) {
    uint8_t parity = 0U;
    for (uint8_t i = 0U; i < 16U; ++i) {
        parity ^= static_cast<uint8_t>((value >> i) & 0x1U);
    }
    return parity;
}

}  // namespace

uint16_t ComputeCrc16Ccitt(const uint8_t* data, size_t length) {
    etl::crc16_ccitt crc;
    crc.add(data, data + length);
    return static_cast<uint16_t>(crc.value());
}

uint32_t ComputeCrc32(const uint8_t* data, size_t length) {
    etl::crc32_mpeg2 crc;
    crc.add(data, data + length);
    return crc.value();
}

uint16_t Secded16Encode11(const uint16_t raw11) {
    // SECDED(16,11): parity bits at positions 1,2,4,8 and overall parity at position 16.
    static constexpr uint8_t kDataPositions[11] = {3U, 5U, 6U, 7U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};

    uint16_t code = 0U;
    for (uint8_t i = 0U; i < 11U; ++i) {
        SetBit(code, kDataPositions[i], static_cast<uint8_t>((raw11 >> i) & 0x1U));
    }

    for (uint8_t parity_pos = 1U; parity_pos <= 8U; parity_pos <<= 1U) {
        uint8_t parity = 0U;
        for (uint8_t pos = 1U; pos <= 15U; ++pos) {
            if ((pos & parity_pos) != 0U) {
                parity ^= GetBit(code, pos);
            }
        }
        SetBit(code, parity_pos, parity);
    }

    // Even parity across all 16 bits.
    SetBit(code, 16U, Parity16(code));
    return code;
}

Secded16DecodeResult Secded16Decode11(uint16_t encoded16) {
    static constexpr uint8_t kDataPositions[11] = {3U, 5U, 6U, 7U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};

    uint8_t syndrome = 0U;
    for (uint8_t parity_pos = 1U; parity_pos <= 8U; parity_pos <<= 1U) {
        uint8_t parity = 0U;
        for (uint8_t pos = 1U; pos <= 15U; ++pos) {
            if ((pos & parity_pos) != 0U) {
                parity ^= GetBit(encoded16, pos);
            }
        }
        if (parity != 0U) {
            syndrome = static_cast<uint8_t>(syndrome | parity_pos);
        }
    }

    const bool overall_error = (Parity16(encoded16) != 0U);
    bool corrected = false;
    bool uncorrectable = false;

    if ((syndrome != 0U) && overall_error) {
        // Correctable single-bit error in positions 1..15.
        encoded16 = static_cast<uint16_t>(encoded16 ^ (1U << (syndrome - 1U)));
        corrected = true;
    } else if ((syndrome == 0U) && overall_error) {
        // Correctable single-bit error in overall parity bit.
        encoded16 = static_cast<uint16_t>(encoded16 ^ (1U << 15U));
        corrected = true;
    } else if ((syndrome != 0U) && !overall_error) {
        // Detected double-bit error.
        uncorrectable = true;
    }

    uint16_t data11 = 0U;
    for (uint8_t i = 0U; i < 11U; ++i) {
        data11 = static_cast<uint16_t>(data11 | (static_cast<uint16_t>(GetBit(encoded16, kDataPositions[i])) << i));
    }

    Secded16DecodeResult result{};
    result.data11 = data11;
    result.corrected = corrected;
    result.uncorrectable = uncorrectable;
    return result;
}

}  // namespace spiopen::algorithms
