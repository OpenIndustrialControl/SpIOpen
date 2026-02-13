# SpIOpen protocol library – CRC-32, Hamming DLC, frame layout (stubs).
# No RTOS/hardware deps; usable from host tests and RP2040/ESP32.
add_library(spiopen_protocol STATIC
    crc32.c
    dlc_hamming.c
    chain_tx_frame.c
)
target_include_directories(spiopen_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
