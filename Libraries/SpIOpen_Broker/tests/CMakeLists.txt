cmake_minimum_required(VERSION 3.14)
project(spiopen_broker_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main
)

FetchContent_MakeAvailable(googletest)

file(GLOB SPIOPEN_BROKER_TESTS_SOURCES "*.cpp")
add_executable(spiopen_broker_tests ${SPIOPEN_BROKER_TESTS_SOURCES})
target_link_libraries(spiopen_broker_tests PRIVATE spiopen_broker gtest gtest_main)

include(GoogleTest)
gtest_discover_tests(spiopen_broker_tests)
