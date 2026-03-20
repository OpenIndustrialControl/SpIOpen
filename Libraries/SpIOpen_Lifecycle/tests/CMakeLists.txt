cmake_minimum_required(VERSION 3.14)
project(spiopen_lifecycle_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main
)

FetchContent_MakeAvailable(googletest)

file(GLOB SPIOPEN_LIFECYCLE_TESTS_SOURCES "*.cpp")
add_executable(spiopen_lifecycle_tests ${SPIOPEN_LIFECYCLE_TESTS_SOURCES})
target_link_libraries(spiopen_lifecycle_tests PRIVATE spiopen_lifecycle gtest gtest_main)

include(GoogleTest)
gtest_discover_tests(spiopen_lifecycle_tests)
