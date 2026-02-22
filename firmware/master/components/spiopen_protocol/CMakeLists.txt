# Wrap lib/spiopen_protocol as an IDF component (built with correct toolchain).
set(SPIOPEN_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib)
idf_component_register(
  SRCS
    "${SPIOPEN_LIB}/spiopen_protocol/crc32.c"
    "${SPIOPEN_LIB}/spiopen_protocol/dlc_hamming.c"
    "${SPIOPEN_LIB}/spiopen_protocol/chain_tx_frame.c"
  INCLUDE_DIRS
    "${SPIOPEN_LIB}/spiopen_protocol"
)
