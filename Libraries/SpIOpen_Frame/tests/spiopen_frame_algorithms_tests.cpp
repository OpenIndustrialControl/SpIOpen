#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "spiopen_frame_algorithms.h"

using namespace spiopen::algorithms;

TEST(SpIOpenAlgorithms, CrcEncodingAccuracy) {
    static constexpr uint8_t example_data_to_crc[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    static constexpr uint16_t expected_crc16 = 0x29B1U;
    static constexpr uint32_t expected_crc32 = 0x0376E6E7U;

    EXPECT_EQ(ComputeCrc16(example_data_to_crc, sizeof(example_data_to_crc)), expected_crc16);
    EXPECT_EQ(ComputeCrc32(example_data_to_crc, sizeof(example_data_to_crc)), expected_crc32);
}

TEST(SpIOpenAlgorithms, SecdedEncodingAccuracy) {
    static constexpr uint16_t kRaw = 0x0123U;  // 0b001'0010'0011
    // check with http://www.mathaddict.net/hamming.htm, but note that they put the parity bits at LSb and we put them
    // at MSb
    static constexpr uint16_t expected_encoded = 0b1000'1001'0010'0011;
    const uint16_t encoded = Secded16Encode11(kRaw);
    EXPECT_EQ(encoded, expected_encoded);
}

TEST(SpIOpenAlgorithms, SecdedRoundTrip) {
    static constexpr uint16_t bitmask_11 = 0x07FFU;
    // loop by a prime number that is approx log(2^11) to cover most of the possible bit organizations
    for (uint16_t raw11 = 0U; raw11 <= bitmask_11; raw11 = static_cast<uint16_t>(raw11 + 73U)) {
        const uint16_t encoded = spiopen::algorithms::Secded16Encode11(raw11);
        const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(encoded);
        EXPECT_EQ(encoded & bitmask_11, raw11);  // verify that the least significant 11 bits remain the same
        EXPECT_FALSE(decoded.corrected);
        EXPECT_FALSE(decoded.uncorrectable);
        EXPECT_EQ(decoded.data11, raw11);
    }
}

TEST(SpIOpenAlgorithms, SecdedSingleBitCorrection) {
    // alternating bits within the 11 bits of relevant data
    static constexpr uint16_t kRaw = 0x0555U;
    const uint16_t encoded = spiopen::algorithms::Secded16Encode11(kRaw);

    // test all possible single bit corruption
    for (uint8_t bit = 0U; bit < 16U; ++bit) {
        const uint16_t corrupted = static_cast<uint16_t>(encoded ^ (1U << bit));
        const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(corrupted);
        EXPECT_EQ(decoded.data11, kRaw);
        EXPECT_TRUE(decoded.corrected);
        EXPECT_FALSE(decoded.uncorrectable);
    }
}

TEST(SpIOpenAlgorithms, SecdedDoubleBitDetect) {
    static constexpr uint16_t kRaw = 0x0555U;
    const uint16_t encoded = spiopen::algorithms::Secded16Encode11(kRaw);

    for (uint8_t i = 0U; i < 16U; ++i) {
        for (size_t j = static_cast<size_t>(i) + 1U; j < 16U; ++j) {
            const uint16_t corrupted = static_cast<uint16_t>(encoded ^ (1U << i) ^ (1U << j));
            const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(corrupted);
            EXPECT_TRUE(decoded.uncorrectable);
        }
    }
}
