cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(spiopen_slave C CXX ASM)
pico_sdk_init()

# FreeRTOS kernel (RP2040 port). Set FREERTOS_KERNEL_PATH or allow fetch.
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory containing FreeRTOSConfig.h")
if (NOT FREERTOS_KERNEL_PATH)
  include(FetchContent)
  FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel
    GIT_TAG main
  )
  FetchContent_Populate(freertos_kernel)
  set(FREERTOS_KERNEL_PATH ${freertos_kernel_SOURCE_DIR} CACHE PATH "Path to FreeRTOS Kernel" FORCE)
endif()
add_subdirectory(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040 ${CMAKE_BINARY_DIR}/FREERTOS_KERNEL)

# SpIOpen protocol and SpIOpen–CANopenNode integration
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/spiopen_protocol ${CMAKE_BINARY_DIR}/spiopen_protocol)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/spiopen_canopen ${CMAKE_BINARY_DIR}/spiopen_canopen)

# CANopenNode (Phase 2: SpIOpen driver, RxPDO1 = RGB LED)
include(FetchContent)
FetchContent_Declare(
  CANopenNode
  GIT_REPOSITORY https://github.com/CANopenNode/CANopenNode
  GIT_TAG master
)
FetchContent_Populate(CANopenNode)
set(CANOPEN_SRC ${canopennode_SOURCE_DIR}/CANopen.c)
file(GLOB CANOPEN_301_SRCS "${canopennode_SOURCE_DIR}/301/*.c")

add_executable(spiopen_slave
  main.c
  canopen.c
  frame_pool.c
  bus_queues.c
  bus_rx_pio.c
  dma_irq.c
  chainbus_tx_spi.c
  chainbus_rx.c
  dropbus_rx.c
  led_rgb_pwm.c
  CO_driver.c
  canopen_od/OD.c
  ${CANOPEN_SRC}
  ${CANOPEN_301_SRCS}
)
pico_enable_stdio_usb(spiopen_slave 1)
pico_generate_pio_header(spiopen_slave ${CMAKE_CURRENT_SOURCE_DIR}/bus_rx_pio.pio)
target_link_libraries(spiopen_slave
  pico_stdlib
  hardware_spi
  hardware_dma
  hardware_pio
  hardware_pwm
  spiopen_canopen
  spiopen_protocol
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap4
)
target_include_directories(spiopen_slave PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/spiopen_protocol
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/spiopen_canopen
  ${CMAKE_CURRENT_SOURCE_DIR}/canopen_config
  ${CMAKE_CURRENT_SOURCE_DIR}/canopen_od
  ${canopennode_SOURCE_DIR}
  ${canopennode_SOURCE_DIR}/301
)
pico_add_extra_outputs(spiopen_slave)
