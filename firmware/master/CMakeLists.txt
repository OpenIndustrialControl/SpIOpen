# SpIOpen CANopen Master – ESP32-C3 Zero (Phase 2)
cmake_minimum_required(VERSION 3.16)
# SpIOpen libs are in components/spiopen_protocol and components/spiopen_canopen (IDF toolchain).
# Fetch CANopenNode at project level (FetchContent not scriptable in main component)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/components)
include(FetchContent)
FetchContent_Declare(
  CANopenNode
  GIT_REPOSITORY https://github.com/CANopenNode/CANopenNode
  GIT_TAG master
)
FetchContent_Populate(CANopenNode)
FetchContent_GetProperties(CANopenNode SOURCE_DIR CANOPEN_NODE_DIR)
if(NOT CANOPEN_NODE_DIR)
  set(CANOPEN_NODE_DIR ${CANopenNode_SOURCE_DIR})
endif()
# Pass to main component via CACHE so it can build canopen_node with correct toolchain
set(CANOPEN_NODE_DIR "${CANOPEN_NODE_DIR}" CACHE PATH "CANopenNode source" FORCE)
set(CANOPEN_301_SRCS
  ${CANOPEN_NODE_DIR}/301/CO_fifo.c
  ${CANOPEN_NODE_DIR}/301/crc16-ccitt.c
  ${CANOPEN_NODE_DIR}/301/CO_TIME.c
  ${CANOPEN_NODE_DIR}/301/CO_SYNC.c
  ${CANOPEN_NODE_DIR}/301/CO_SDOserver.c
  ${CANOPEN_NODE_DIR}/301/CO_SDOclient.c
  ${CANOPEN_NODE_DIR}/301/CO_PDO.c
  ${CANOPEN_NODE_DIR}/301/CO_ODinterface.c
  ${CANOPEN_NODE_DIR}/301/CO_Node_Guarding.c
  ${CANOPEN_NODE_DIR}/301/CO_NMT_Heartbeat.c
  ${CANOPEN_NODE_DIR}/301/CO_HBconsumer.c
  ${CANOPEN_NODE_DIR}/301/CO_Emergency.c
)
set(CANOPEN_MASTER_SRCS
  ${CANOPEN_NODE_DIR}/CANopen.c
  ${CANOPEN_301_SRCS}
  ${CANOPEN_NODE_DIR}/305/CO_LSSmaster.c
  ${CANOPEN_NODE_DIR}/309/CO_gateway_ascii.c
)
set(CANOPEN_MASTER_SRCS "${CANOPEN_MASTER_SRCS}" CACHE LIST "CANopenNode sources" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spiopen_master)
