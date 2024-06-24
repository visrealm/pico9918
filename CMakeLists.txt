cmake_minimum_required(VERSION 3.12)

# pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# pull in helpers for my custom tools
include(visrealm_tools.cmake)

set(BUILD_SHARED_LIBS OFF)
set(PICO_BUILD 1)

set(PROJECT pico9918)

project(${PROJECT} C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

add_subdirectory(submodules/vrEmuTms9918)
add_subdirectory(src)
add_subdirectory(test)

