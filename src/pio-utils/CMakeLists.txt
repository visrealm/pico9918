cmake_minimum_required(VERSION 3.12)

set(LIBRARY pico9918-pio-utils)

project (${LIBRARY} C)

set(CMAKE_C_STANDARD 11)

add_library(${LIBRARY} STATIC pio_utils.c)

target_include_directories (${LIBRARY} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${LIBRARY} PUBLIC hardware_pio pico_stdlib)

target_compile_options(${PROJECT_NAME} PUBLIC -include pico/platform/compiler.h)