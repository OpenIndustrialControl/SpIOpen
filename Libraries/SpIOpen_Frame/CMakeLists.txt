cmake_minimum_required(VERSION 3.14)
project(spiopen_frame CXX)

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

# Collect all files for this library and create the library
file(GLOB_RECURSE SUBMODULE_SOURCES "src/*.cpp")
file(GLOB_RECURSE SUBMODULE_HEADERS "include/*.h" "include/*.hpp")
add_library(spiopen_frame STATIC ${SUBMODULE_SOURCES} ${SUBMODULE_HEADERS})

# Let other parts of the project see the public includes
target_include_directories(spiopen_frame PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ETL (Embedded Template Library) for CRC - header-only, private to this library
target_include_directories(spiopen_frame PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/embeded-template-library/include
)

option(SPIOPEN_FRAME_BUILD_TESTS "Build unit tests for the library" OFF)
if(SPIOPEN_FRAME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()