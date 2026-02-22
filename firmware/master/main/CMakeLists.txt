# SpIOpen master – main component (ESP32-C3, FreeRTOS, CANopenNode, SpIOpen transport)
# CANOPEN_NODE_DIR and CANOPEN_MASTER_SRCS are set in project root (CACHE).
set(CMAKE_C_STANDARD 11)

idf_component_register(
  SRCS
    "main.c"
    "frame_pool.c"
    "spiopen_queues.c"
    "spiopen_tx_spi.c"
    "spiopen_rx_slave.c"
    "CO_driver.c"
    "canopen_od/OD.c"
    ${CANOPEN_MASTER_SRCS}
  INCLUDE_DIRS
    "."
    ".."
    "canopen_config"
    "canopen_od"
    "${CANOPEN_NODE_DIR}"
    "${CANOPEN_NODE_DIR}/301"
    "${CANOPEN_NODE_DIR}/303"
    "${CANOPEN_NODE_DIR}/304"
    "${CANOPEN_NODE_DIR}/305"
    "${CANOPEN_NODE_DIR}/309"
    "${CANOPEN_NODE_DIR}/extra"
  PRIV_REQUIRES
    driver
    esp_driver_gpio
    esp_driver_i2s
    freertos
    esp_timer
    spiopen_canopen
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-error=format)
