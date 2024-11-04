# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards )
set(PICO_BOARD pico9918_v04 CACHE STRING "Board type")
#set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# pull in helpers for my custom tools
include(visrealm_tools.cmake)

project(pico9918 C CXX ASM)

add_definitions(-DPICO_BUILD=1)
add_definitions(-DVR_EMU_TMS9918_SINGLE_INSTANCE=1)
add_definitions(-DVR_EMU_TMS9918_MODE=1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(submodules/vrEmuTms9918)
add_subdirectory(src)
add_subdirectory(test)