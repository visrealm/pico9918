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
set(PICO_BOARD pico9918 CACHE STRING "Board type")
#set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# pull in helpers for my custom tools
include(visrealm_tools.cmake)

project(pico9918 C CXX ASM)

add_definitions(-DPICO_BUILD=1)
add_definitions(-DVR_EMU_TMS9918_SINGLE_INSTANCE=1)
add_definitions(-DVR_EMU_TMS9918_MODE=1)

# Standard boot configures SYS_CLK as 125MHz, but we want boot2 to use
# PICO_FLASH_SPI_CLKDIV=4 so we can update the firmware during runtime,
# especially when overclocking more for F18A mode.  This 4 x DIV would be
# slower to "Copy to RAM" at startup, but specifying the minimum Pico9918
# speed here solves that additional delay issue.

#add_definitions(-DSYS_CLK_HZ=252000000)
#add_definitions(-DPLL_SYS_VCO_FREQ_HZ=1260000000)
#add_definitions(-DPLL_SYS_POSTDIV1=5)
#add_definitions(-DPLL_SYS_POSTDIV2=1)
# SDK Flash write routines exit XIP mode and use boot2 to reenter it, so
# boot2 needs to be properly configured for whatever speed we end up at.
#add_definitions(-DPICO_FLASH_SPI_CLKDIV=2)
add_definitions(-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=32)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(submodules/vrEmuTms9918)
add_subdirectory(src)
add_subdirectory(test)