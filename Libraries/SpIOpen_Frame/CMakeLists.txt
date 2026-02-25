cmake_minimum_required(VERSION 3.14)
project(spiopen_frame CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TODO: Get KConfig included

# Collect all files for this library and create the library
file(GLOB_RECURSE SUBMODULE_SOURCES "src/*.cpp")
file(GLOB_RECURSE SUBMODULE_HEADERS "include/*.h" "include/*.hpp")
add_library(spiopen_frame STATIC ${SUBMODULE_SOURCES} ${SUBMODULE_HEADERS})

# Let other parts of the project see the public includes
target_include_directories(spiopen_frame PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

option(SPIOPEN_FRAME_BUILD_TESTS "Build unit tests for the library" OFF)
if(SPIOPEN_FRAME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()