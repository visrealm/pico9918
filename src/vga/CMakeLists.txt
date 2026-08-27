cmake_minimum_required(VERSION 3.12)

set(LIBRARY pico9918-vga)

project (${LIBRARY} C)

set(CMAKE_C_STANDARD 11)

add_library(${LIBRARY} STATIC vga.c vga-modes.c)

# generate header file from pio
pico_generate_pio_header(${LIBRARY} ${CMAKE_CURRENT_LIST_DIR}/vga.pio)

target_include_directories (${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(${LIBRARY} PRIVATE
        PICO9918_ENABLE_SCART=$<BOOL:${PICO9918_ENABLE_SCART}>
        PICO9918_COLD_IN_FLASH=$<BOOL:${PICO9918_COLD_IN_FLASH}>
        PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)

# pico9918_core: the palette guard. The MPU the GPU faults on belongs to core 0,
# and its scanline interrupt is the only place that core is idle - see vga.c.
target_link_libraries(${LIBRARY} PRIVATE
        pico9918_core
        pico9918-pio-utils
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma)