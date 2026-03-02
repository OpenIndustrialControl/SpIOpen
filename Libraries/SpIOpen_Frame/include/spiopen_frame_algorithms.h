/*
SpIOpen Frame Algorithm Facade

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0

Single public header for CRC and SECDED algorithms. The implementation is
chosen at compile time by linking the appropriate .cpp (see AlgorithmBackend.md).
No runtime indirection—calls resolve directly to the linked implementation.
*/
#pragma once

#include <cstddef>
#include <cstdint>

namespace spiopen::algorithms {

struct Secded16DecodeResult {
    uint16_t data11;
    bool corrected;
    bool uncorrectable;
};

uint16_t ComputeCrc16(const uint8_t* data, size_t length);
uint32_t ComputeCrc32(const uint8_t* data, size_t length);

// The goal with the SECDED encoding should be to make the "typical" path (no errors) as fast as possible.
uint16_t Secded16Encode11(uint16_t raw11);
Secded16DecodeResult Secded16Decode11(uint16_t encoded16);

}  // namespace spiopen::algorithms
