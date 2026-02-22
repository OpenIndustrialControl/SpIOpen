# Wrap lib/spiopen_canopen as an IDF component (built with correct toolchain).
set(SPIOPEN_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib)
idf_component_register(
  SRCS
    "${SPIOPEN_LIB}/spiopen_canopen/spiopen_canopen.c"
  INCLUDE_DIRS
    "${SPIOPEN_LIB}/spiopen_canopen"
  REQUIRES
    spiopen_protocol
)
