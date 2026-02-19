# SpIOpen ↔ CANopenNode integration (parse/build; shared by slave and master).
add_library(spiopen_canopen STATIC
    spiopen_canopen.c
)
target_include_directories(spiopen_canopen PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(spiopen_canopen PUBLIC spiopen_protocol)
