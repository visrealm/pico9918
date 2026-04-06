cmake_minimum_required(VERSION 3.12)

set(LIBRARY pico9918-vga)

project (${LIBRARY} C)

set(CMAKE_C_STANDARD 11)

add_library(${LIBRARY} STATIC vga.c vga-modes.c)

# generate header file from pio
pico_generate_pio_header(${LIBRARY} ${CMAKE_CURRENT_LIST_DIR}/vga.pio)

target_include_directories (${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${LIBRARY} PRIVATE
        pico9918-pio-utils
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma)