cmake_minimum_required(VERSION 3.14)
project(spiopen_message CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create KConfig h file from .config file, then auto-include it in the build
execute_process(COMMAND python3 /usr/local/lib/python3.10/dist-packages/genconfig.py
--header-path ${CMAKE_CURRENT_SOURCE_DIR}/include/generated/autoconf.h
WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
RESULT_VARIABLE GENCONFIG_RESULT
ERROR_VARIABLE GENCONFIG_ERROR)

if(NOT GENCONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate KConfig header: ${GENCONFIG_ERROR}")
endif()

add_compile_options(-imacros${CMAKE_CURRENT_SOURCE_DIR}/include/generated/autoconf.h)

# Collect library sources
file(GLOB SPIOPEN_MESSAGE_SOURCES "src/*.cpp")
file(GLOB SPIOPEN_MESSAGE_HEADERS "include/*.h" "include/*.hpp")

add_library(spiopen_message STATIC ${SPIOPEN_MESSAGE_SOURCES} ${SPIOPEN_MESSAGE_HEADERS})

# Let other parts of the project see the public includes
target_include_directories(spiopen_message PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/embeded-template-library/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/SpIOpen_Frame/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/SpIOpen_Lifecycle/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/CMSIS/CMSIS/RTOS2/Include
)

# Link frame library once concrete broker implementations depend on frame .cpp symbols.
# target_link_libraries(spiopen_message PUBLIC spiopen_frame)

option(BUILD_TESTING "Build unit tests for the library" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
