/*
SpIOpen Frame Format : Constants defining the format of a SpIOpen frame. See @FrameFrormat.md for full specification.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once
#include <cstddef>
#include <cstdint>

namespace spiopen::format {
/* Preamble Constants */
static constexpr uint8_t PREAMBLE_BYTE = 0xAA;             // Value of each byte in the preamble
static constexpr uint8_t PREAMBLE_BYTE_COMPLEMENT = 0x55;  // Value of the complement of the preamble byte
static constexpr uint16_t PREAMBLE_WORD = 0xAAAA;          // Value of the preamble word

/* Macros that define byte sizes and offsets for all of the elements of a SpIOpen frame */
static constexpr size_t PREAMBLE_SIZE = 2;        // Size of the preamble in bytes
static constexpr size_t FORMAT_HEADER_SIZE = 2;   // Size of the format header in bytes
static constexpr size_t XL_DATA_LENGTH_SIZE = 2;  // Size of the XL data length field)
static constexpr size_t XL_CONTROL_SIZE = 6;      // Size of the XL control field in bytes (Less the data length field)
static constexpr size_t CAN_IDENTIFIER_SIZE = 2;  // Size of the basic CAN identifier in bytes
static constexpr size_t CAN_IDENTIFIER_EXTENSION_SIZE =
    2;  // Size of additional extended identifier in bytes (Only used if IDE flag is set)
static constexpr size_t TIME_TO_LIVE_SIZE =
    1;  // Size of the Time to Live counter in bytes (Only used if TTL flag is set)
static constexpr size_t MAX_CC_PAYLOAD_SIZE = 8;   // Maximum payload size in bytes for CAN-CC frames
static constexpr size_t MAX_FD_PAYLOAD_SIZE = 64;  // Maximum payload size in bytes for CAN-FD frames

static constexpr size_t MAX_XL_PAYLOAD_SIZE = 2048;  // Maximum payload size in bytes for CAN-XL frames
static constexpr size_t SHORT_CRC_SIZE = 2;          // Size of the CRC16 checksum in bytes (Payloads <= 8 Bytes long)
static constexpr size_t LONG_CRC_SIZE = 4;           // Size of the CRC32 checksum in bytes (Payloads > 8 Bytes long)
static constexpr size_t MAX_PADDING_SIZE =
    1;  // Maximum size of the padding byte in bytes (Only used if WA flag is set)

/* Macros that define the byte masks for the various bit-stuffed flags and sub-integers of the header */
static constexpr uint8_t HEADER_DLC_MASK = 0x0F;  // Mask for the DLC field in the low byte of the format header
static constexpr uint8_t HEADER_IDE_MASK = 0x10;  // Mask for the IDE field in the low byte of the format header
static constexpr uint8_t HEADER_FDF_MASK = 0x20;  // Mask for the FDF field in the low byte of the format header
static constexpr uint8_t HEADER_XLF_MASK = 0x40;  // Mask for the XLF field in the low byte of the format header
static constexpr uint8_t HEADER_TTL_MASK = 0x80;  // Mask for the TTL field in the low byte of the format header
static constexpr uint8_t HEADER_WA_MASK =
    0x01;  // Mask for the word alignment field in the high byte of the format header
static constexpr uint8_t CID_RTR_MASK = 0x80;  // Mask for the RTR/RRS field in the highest byte of the CAN identifier
static constexpr uint8_t CID_BRS_MASK = 0x40;  // Mask for the BRS field in the highest byte of the CAN identifier
static constexpr uint8_t CID_ESI_MASK = 0x20;  // Mask for the ESI field in the highest byte of the CAN identifier

/* CAN-FD 4-bit DLC to payload length (bytes): 0-8, 12, 16, 20, 24, 32, 48, 64 */
static constexpr size_t CAN_FD_PAYLOAD_BY_DLC[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

// Max sizes of relevant data segments of a SpIOpen frame, used to calculate buffer alignment
static constexpr size_t MAX_CAN_CC_HEADER_SIZE =
    (PREAMBLE_SIZE + FORMAT_HEADER_SIZE + CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE + TIME_TO_LIVE_SIZE);
static constexpr size_t MAX_CAN_FD_HEADER_SIZE =
    (PREAMBLE_SIZE + FORMAT_HEADER_SIZE + CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE + TIME_TO_LIVE_SIZE);
static constexpr size_t MAX_CAN_XL_HEADER_SIZE =
    (PREAMBLE_SIZE + FORMAT_HEADER_SIZE + CAN_IDENTIFIER_SIZE + CAN_IDENTIFIER_EXTENSION_SIZE + TIME_TO_LIVE_SIZE +
     XL_DATA_LENGTH_SIZE + XL_CONTROL_SIZE);

static constexpr size_t MAX_CAN_CC_FRAME_SIZE =
    (MAX_CAN_CC_HEADER_SIZE + MAX_CC_PAYLOAD_SIZE + SHORT_CRC_SIZE + MAX_PADDING_SIZE);
static constexpr size_t MAX_CAN_FD_FRAME_SIZE =
    (MAX_CAN_FD_HEADER_SIZE + MAX_FD_PAYLOAD_SIZE + LONG_CRC_SIZE + MAX_PADDING_SIZE);
static constexpr size_t MAX_CAN_XL_FRAME_SIZE =
    (MAX_CAN_XL_HEADER_SIZE + MAX_XL_PAYLOAD_SIZE + LONG_CRC_SIZE + MAX_PADDING_SIZE);
}  // namespace spiopen::format