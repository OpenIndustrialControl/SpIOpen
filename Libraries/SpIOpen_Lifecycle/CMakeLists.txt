cmake_minimum_required(VERSION 3.14)
project(spiopen_lifecycle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect library sources
file(GLOB SPIOPEN_LIFECYCLE_SOURCES "src/*.cpp")
file(GLOB SPIOPEN_LIFECYCLE_HEADERS "include/*.h" "include/*.hpp")

add_library(spiopen_lifecycle STATIC ${SPIOPEN_LIFECYCLE_SOURCES} ${SPIOPEN_LIFECYCLE_HEADERS})

# Let other parts of the project see the public includes
target_include_directories(spiopen_lifecycle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/embeded-template-library/include
)

option(BUILD_TESTING "Build unit tests for the library" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
