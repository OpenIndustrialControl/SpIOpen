#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "spiopen_frame_algorithms.h"

using namespace spiopen::algorithms;

TEST(SpIOpenAlgorithms, DefaultCrcKnownVectors) {
    static constexpr uint8_t kData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    // CRC16-CCITT and CRC32/MPEG-2 known vectors for "123456789".
    EXPECT_EQ(spiopen::algorithms::ComputeCrc16Ccitt(kData, sizeof(kData)), 0x29B1U);
    EXPECT_EQ(spiopen::algorithms::ComputeCrc32(kData, sizeof(kData)), 0x0376E6E7U);
}

TEST(SpIOpenAlgorithms, SecdedRoundTrip) {
    for (uint16_t raw11 = 0U; raw11 <= 0x07FFU; raw11 = static_cast<uint16_t>(raw11 + 73U)) {
        const uint16_t encoded = spiopen::algorithms::Secded16Encode11(raw11);
        const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(encoded);
        EXPECT_EQ(decoded.data11, raw11);
        EXPECT_FALSE(decoded.corrected);
        EXPECT_FALSE(decoded.uncorrectable);
    }
}

TEST(SpIOpenAlgorithms, SecdedSingleBitCorrection) {
    static constexpr uint16_t kRaw = 0x0555U;
    const uint16_t encoded = spiopen::algorithms::Secded16Encode11(kRaw);

    for (uint8_t bit = 0U; bit < 16U; ++bit) {
        const uint16_t corrupted = static_cast<uint16_t>(encoded ^ (1U << bit));
        const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(corrupted);
        EXPECT_EQ(decoded.data11, kRaw);
        EXPECT_TRUE(decoded.corrected);
        EXPECT_FALSE(decoded.uncorrectable);
    }
}

TEST(SpIOpenAlgorithms, SecdedDoubleBitDetect) {
    static constexpr uint16_t kRaw = 0x0123U;
    const uint16_t encoded = spiopen::algorithms::Secded16Encode11(kRaw);
    const uint16_t corrupted = static_cast<uint16_t>(encoded ^ 0x0003U);
    const Secded16DecodeResult decoded = spiopen::algorithms::Secded16Decode11(corrupted);
    EXPECT_TRUE(decoded.uncorrectable);
}
