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
#include "etl/span.h"

namespace spiopen::algorithms {

uint16_t ComputeCrc16(const etl::span<const uint8_t>& data) {
    etl::crc16_ccitt crc;
    crc.add(data.begin(), data.end());
    return static_cast<uint16_t>(crc.value());
}

uint32_t ComputeCrc32(const etl::span<const uint8_t>& data) {
    etl::crc32_mpeg2 crc;
    crc.add(data.begin(), data.end());
    return crc.value();
}

// #TODO: use more etl::binary functionality to repalce a lot of these bitwise operations

// use constants to trade memory for speed:
static constexpr uint16_t secded16_num_data_bits = 11U;
static constexpr uint16_t secded16_num_parity_bits = 5U;
static constexpr uint16_t secded16_data_bit_mask = static_cast<uint16_t>(0xFFFFU) >> secded16_num_parity_bits;
static constexpr std::array<uint16_t, 5> secded16_partiy_data_masks = {
    0b0000'0101'0101'1011,   // hamming parity bit 0, encoding bits [0,1,3,4,6,8,10]
    0b0000'0110'0110'1101,   // hamming parity bit 1, encoding bits [0,2,3,5,6,9,10]
    0b0000'0111'1000'1110,   // hamming parity bit 2, encoding bits [1,2,3,7,8,9,10]
    0b0000'0111'1111'0000,   // hamming parity bit 3, encoding bits [4,5,6,7,8,9,10]
    0b0111'1111'1111'1111};  // overall parity encoding all data bits and parity bits
static constexpr std::array<uint16_t, 5> secded16_parity_bit_position_masks = {
    1U << (secded16_num_data_bits + 0U), 1U << (secded16_num_data_bits + 1U), 1U << (secded16_num_data_bits + 2U),
    1U << (secded16_num_data_bits + 3U), 1U << (secded16_num_data_bits + 4U)};
static constexpr uint16_t secded16_syndrome_mask =
    secded16_parity_bit_position_masks[0] | secded16_parity_bit_position_masks[1] |
    secded16_parity_bit_position_masks[2] | secded16_parity_bit_position_masks[3];
static constexpr uint16_t secded16_overall_parity_mask = secded16_parity_bit_position_masks[4];
static constexpr uint8_t secded16_syndrome_to_data_bit_mapping[secded16_num_data_bits + secded16_num_parity_bits] = {
    11U, 12U, 0U, 13U, 1U, 2U, 3U, 14U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};  // 0-based indices

// SECDED(16,11) systematic encoding
// The data bits are placed in the least significant positions
// in the encoded word. Hamming parity bits are palced at bit positions 12-15, and the overall parity bit is placed at
// bit position 16. Bit number 1 is the least significant.
uint16_t Secded16Encode11(const uint16_t raw11) {
    uint16_t code = raw11 & secded16_data_bit_mask;
    for (uint8_t parity_bit_index = 0U; parity_bit_index < secded16_num_parity_bits; ++parity_bit_index) {
        if (__builtin_popcount(code & secded16_partiy_data_masks[parity_bit_index]) &
            1U) {  // TRUE if odd number of bits in the group are true
            code |= secded16_parity_bit_position_masks[parity_bit_index];  // set the parity bit to 1 to ensure even
                                                                           // parity in the group
        }
    }
    return code;
}

// SECDED(16,11) systematic decoding
// The data bits are placed in the least significant positions
// in the encoded word. Hamming parity bits are palced at bit positions 12-15, and the overall parity bit is placed at
// bit position 16. Bit number 1 is the least significant.
Secded16DecodeResult Secded16Decode11(uint16_t encoded16) {
    Secded16DecodeResult result{};
    result.data11 = encoded16 & secded16_data_bit_mask;
    result.corrected = false;
    result.uncorrectable = false;
    uint16_t reencoded16 = Secded16Encode11(result.data11);
    if (encoded16 == reencoded16) {
        return result;
    }

    if (__builtin_popcount(encoded16) % 2U !=
        0) {  // overall parity is incorrect so there must be an odd number of errors
        // Assume there is exactly one error and correct it using the syndrome.
        uint16_t syndrome = (encoded16 ^ reencoded16) & secded16_syndrome_mask;
        if (syndrome != 0U) {  // syndrome is non-zero, so we have a single-bit error in the data or syndrome bits
            syndrome >>=
                secded16_num_data_bits;  // syndrome now points to the 1-based data bit in the interleaved
                                         // secded method which needs to be corrected. Use a lookup table to account for
                                         // the difference between bit positions in the interleaved method (easy
                                         // correciton math) and the systematic method (used on the wire)
            size_t error_bit_position = secded16_syndrome_to_data_bit_mapping[syndrome - 1U];  // 0 based index
            result.corrected = true;
            result.data11 = (encoded16 ^ (1U << error_bit_position)) & secded16_data_bit_mask;
        } else {  // syndrome is zero, so the overall parity is the only error. Don't bother correcting it because we
                  // don't even return it.
            result.corrected = true;
        }
    } else {  // overall parity is correct but the syndrome doesn't match, so there must be an even number of errors
        result.uncorrectable = true;  // even numbers of errors, even two, are not correctable
    }

    return result;
}
}  // namespace spiopen::algorithms
