The SpIOpen frame is meant to be a compact representation of any CAN, CAN-FD, or CAN-XL frame, but reconfigured for optimal throughout of SPI enabled microcontrollers. 
* Data lengths are protected with error-correcting-codes and the entire frame is protected with cyclic redundancy checks. 
* Bit stuffing is not required because SPI is a clock driven protocol, not a differential protocol.
* Bit order is not as important because there is no electrical arbitration taking place

The Data Format of a SpIOpen Frame:
* [2 Bytes] Preamble : 0xAAAA
    * Useful for finding the beginning of a new frame in a large buffer or in a PIO state machine
* [2 Bytes] Format Header : Contains info on the rest of the header format, and everything needed to calculate the frame length
    * Most significant byte first
    * SECDED(16,11) : 11 bits of data hamming-encoded to 16 bits on-the-wire such that 1 error can be corrected and two can be detected
    * [Bits 0-3] : CAN Data Length Code ("DLC") - Along with other flags, indicated the payload length
    * [Bit 4] : Identifier Extension ("IDE") flag - TRUE indicates that the 29-bit CID (4 Bytes) is used instead of the default 11-bit CID (2 Bytes)
    * [Bit 5] : Flexible Data-Rate Format ("FDF") flag - TRUE indicates that CAN-FD features are available (BRS, ESI, Extended DLC)
    * [Bit 6] : XL Format ("XLF") flag - TRUE indicates that CAN-XL features are available and the 8-Byte XL control
    * [Bit 7] : Time to Live ("TTL") flag - TRUE Indicates that the 1 Byte TTL counter is included (and should be decremented after receipt)
    * [Bit 8] : Word Alignment flag - TRUE indicates that the frame will be padded to ensure the total byte count is a multiple of *two*
    * [Bit 9] : Reserved
    * [Bit 10] : Reserved
* [0 or 8 Bytes] XL Control (only present if XLF flag is set)
    * [Byte 0-1] : Data Length Code, hamming encoded 11 bits into 16 bits ("DLC") - replacing earlier DLC
    * [Byte 2] : Payload Type ("SDT")
    * [Byte 3] : Virtual Can Network ID ("VCID")
    * [Byte 4-7] : Addressing Field ("AF")
* [2 or 4 Bytes] CAN Identifier ("CID") and flags
    * Most significant byte first
    * If Header IDE flag is *not* set (typical case) contains the 11 bit CID in 2 bytes
    * If header IDE flag *is* set, contains the 29 bit CID in 4 bytes
    * The most significant three bits contain additional CAN and CAN-FD flags:
      * [Bit N] : Remote Transmition Request ("RTR") / Remote Request Substitution ("RRS") Flag
      * [Bit N-1] : Bit Rate Switch ("BRS") flag
      * [Bit N-2] : Error Status Indicator ("ESI") flag
* [0 or 1 Byte] Time to Live ("TTL")
    * Only present if header "TTL" flag is set
    * Decremented every time the frame passes through a SpIOpen Device
    * Useful for preventing loops, or doing relative-physical-position determination on a daisy chain bus
* [0 to 8/64/2048 Bytes] Data
  * IF XLF Flag : Interpret data length per XL Control Data Length Code (0-2048)
  * ELSE IF FDF Flag : Interpret data length per 4-bit DLC {0-8, 12, 16, 20, 24, 32, 48, 64}
  * ELSE : Interpret data length per 4-bit DLC, capped at 8 bytes
* [0 or 1 Bytes] Padding : 0x00 IF
* [2 or 4 Bytes] CRC
  * CRC16 if Data Length <= 8 (CRC-16-CCITT)
  * CRC32 if Data Length > 8 (CRC-32/ISO-HDLC)

Notes:
* Reading the first 4 bytes of data after the preamble is always possible (6 is the minimum) and can always determine the length of the entire frame
* Question: Should there be a minimum inter-packet gap?
* Question: Should there be padding for 2-byte or 4-byte alignment to help densify hardware accelerated FIFOs, DMA, etc?
