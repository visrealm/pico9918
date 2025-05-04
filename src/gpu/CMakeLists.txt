cmake_minimum_required(VERSION 3.12)

set(LIBRARY pico9918-gpu)

project (${LIBRARY} C)

set(CMAKE_C_STANDARD 11)

if(PICO_PLATFORM STREQUAL "rp2040")
        set(PICO9918_ASM_SUFFIX "_m0")
else()
        set(PICO9918_ASM_SUFFIX "_m33")
endif()

add_library(${LIBRARY} STATIC gpu.c thumb9900${PICO9918_ASM_SUFFIX}.S)

target_include_directories (${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${LIBRARY} PRIVATE
        pico_stdlib
        hardware_flash
        vrEmuTms9918)        