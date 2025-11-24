# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards )
set(PICO_BOARD pico2_w CACHE STRING "Board type")
#set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")

# Check if building configurator only (skip Pico SDK setup)
if(NOT CONFIGURATOR_ONLY)
    # Pull in Raspberry Pi Pico SDK (must be before project)
    include(pico_sdk_import.cmake)
endif()

# pull in helpers for my custom tools
include(visrealm_tools.cmake)
include(visrealm_cvbasic.cmake)

project(pico9918 C CXX ASM)

# PICO9918 Version Information
set(PICO9918_MAJOR_VER 1)
set(PICO9918_MINOR_VER 0)
set(PICO9918_PATCH_VER 2)
set(PICO9918_VERSION "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

# PICO9918 Build Options
option(PICO9918_SCART_RGBS "Enable SCART RGBs output (default: VGA)" OFF)
option(PICO9918_SCART_PAL "Use PAL 576i timing (default: NTSC 480i)" OFF)
option(PICO9918_NO_SPLASH "Disable splash screen" OFF)
option(PICO9918_DIAG "Enable diagnostic mode" OFF)

add_definitions(-DPICO_BUILD=1)
add_definitions(-DVR_EMU_TMS9918_SINGLE_INSTANCE=1)
add_definitions(-DVR_EMU_TMS9918_MODE=1)


add_definitions(-DSYS_CLK_HZ=252000000)
add_definitions(-DPLL_SYS_VCO_FREQ_HZ=1512000000)
add_definitions(-DPLL_SYS_POSTDIV1=6)
add_definitions(-DPLL_SYS_POSTDIV2=1)

# SDK Flash write routines exit XIP mode and use boot2 to reenter it, so
# boot2 needs to be properly configured for whatever speed we end up at.
add_definitions(-DPICO_FLASH_SPI_CLKDIV=4)      # This could be 2, but then we can't write to firmware at higher clocks
add_definitions(-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=8)


if(CONFIGURATOR_ONLY)
    add_subdirectory(configtool)
else()
    # Initialise the Raspberry Pi Pico SDK
    pico_sdk_init()

    add_subdirectory(submodules/vrEmuTms9918)
    add_subdirectory(src)
    #add_subdirectory(test)
    #add_subdirectory(configtool)
endif()