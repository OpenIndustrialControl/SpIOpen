cmake_minimum_required(VERSION 3.14)
project(spiopen_frame_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main # Use "main" or a specific commit hash/tag
)

FetchContent_MakeAvailable(googletest)

add_executable(spiopen_frame_tests spiopen_frame_tests.cpp)
target_link_libraries(spiopen_frame_tests PRIVATE gtest gtest_main)]

# automatically discover CTest tests in the project
include(GoogleTest)
gtest_discover_tests(spiopen_frame_tests)