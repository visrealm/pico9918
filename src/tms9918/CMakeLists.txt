cmake_minimum_required(VERSION 3.12)

set(LIBRARY pico9918-tms9918)

add_subdirectory(vga)

project (${LIBRARY} C)

set(CMAKE_C_STANDARD 11)

add_library(${LIBRARY} STATIC tms9918.c)

target_include_directories (${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${LIBRARY} PUBLIC
        pico9918-vga
        pico_stdlib
        vrEmuTms9918
        vrEmuTms9918Util)