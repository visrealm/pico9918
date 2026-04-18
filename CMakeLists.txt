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
set(PICO_BOARD pico9918pro CACHE STRING "Board type")
#set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")

option(PICO9918_BUILD_COMBINED "Build combined UF2 for both pico9918 (RP2040) and pico9918pro (RP2350)" OFF)

# Check if building configurator only (skip Pico SDK setup)
if(NOT CONFIGURATOR_ONLY AND NOT PICO9918_BUILD_COMBINED)
    # Pull in Raspberry Pi Pico SDK (must be before project)
    include(pico_sdk_import.cmake)
endif()

# pull in helpers for my custom tools
include(visrealm_tools.cmake)
include(visrealm_cvbasic.cmake)

if(PICO9918_BUILD_COMBINED OR CONFIGURATOR_ONLY)
    project(pico9918 NONE)
else()
    project(pico9918 C CXX ASM)
endif()

# PICO9918 Version Information
set(PICO9918_MAJOR_VER 1)
set(PICO9918_MINOR_VER 2)
set(PICO9918_PATCH_VER 0)

set(PICO9918_VERSION "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

# PICO9918 Build Options
option(PICO9918_SCART_RGBS "Enable SCART RGBs output (default: VGA)" OFF)
option(PICO9918_SCART_PAL "Use PAL 576i timing (default: NTSC 480i)" OFF)
option(PICO9918_SCART_AUTODETECT "Runtime SCART dongle detection via GPIO pull test" OFF)
option(PICO9918_NO_SPLASH "Disable splash screen" OFF)
option(PICO9918_DIAG "Enable diagnostic mode" OFF)

add_definitions(-DPICO_BUILD=1)
add_definitions(-DVR_EMU_TMS9918_SINGLE_INSTANCE=1)
add_definitions(-DVR_EMU_TMS9918_MODE=1)


if(PICO9918_SCART_RGBS AND NOT PICO9918_SCART_AUTODETECT)
  # 270 MHz: exact integer pioClocksPerPixel=20 for 13.5 MHz PAL/NTSC pixel clock
  add_definitions(-DSYS_CLK_HZ=270000000)
  add_definitions(-DPLL_SYS_VCO_FREQ_HZ=1080000000)
  add_definitions(-DPLL_SYS_POSTDIV1=4)
  add_definitions(-DPLL_SYS_POSTDIV2=1)
else()
  # VGA 252 MHz (also the boot default for autodetect; re-clocked if SCART detected)
  add_definitions(-DSYS_CLK_HZ=252000000)
  add_definitions(-DPLL_SYS_VCO_FREQ_HZ=1512000000)
  add_definitions(-DPLL_SYS_POSTDIV1=6)
  add_definitions(-DPLL_SYS_POSTDIV2=1)
endif()

# SDK Flash write routines exit XIP mode and use boot2 to reenter it, so
# boot2 needs to be properly configured for whatever speed we end up at.
add_definitions(-DPICO_FLASH_SPI_CLKDIV=4)      # This could be 2, but then we can't write to firmware at higher clocks
add_definitions(-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=8)


# set up variables for the build process

execute_process(
    COMMAND git symbolic-ref --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PICO9918_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT CONFIGURATOR_ONLY)
  if(PICO9918_SCART_AUTODETECT)
    set(PICO9918_OUTPUT_STR "autodetect")
    set(PICO9918_OUTPUT_PREFIX "-autodetect")
  elseif(PICO9918_SCART_RGBS)
    if (PICO9918_SCART_PAL)
      set(PICO9918_OUTPUT_STR "rgbs-pal576i")
    else()
      set(PICO9918_OUTPUT_STR "rgbs-ntsc480i")
    endif()
    set(PICO9918_OUTPUT_PREFIX "-${PICO9918_OUTPUT_STR}")
  else()
    set(PICO9918_OUTPUT_STR "vga")
    set(PICO9918_OUTPUT_PREFIX "")
  endif()
endif()

set(PICO9918_VERSION  "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

if (${PICO_BOARD} STREQUAL "pico9918pro")
  set(PICO_BOARD_STR pro)
elseif (NOT ${PICO_BOARD} STREQUAL "pico9918")
  set(PICO_BOARD_STR -${PICO_BOARD})
endif()

# Include branch name in binary suffix (except for main branch)
set(PICO9918_BRANCH_STR "")
if (NOT PICO9918_GIT_BRANCH STREQUAL "main" AND NOT PICO9918_GIT_BRANCH STREQUAL "")
  string(REPLACE "/" "-" PICO9918_GIT_BRANCH_SAFE "${PICO9918_GIT_BRANCH}")
  set(PICO9918_BRANCH_STR "-${PICO9918_GIT_BRANCH_SAFE}")
endif()

set(PICO9918_BINARY_SUFFIX ${PICO_BOARD_STR}-${PICO9918_OUTPUT_STR}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR})

if (PICO9918_DIAG)
  set(PICO9918_BINARY_SUFFIX ${PICO9918_BINARY_SUFFIX}-diag)
endif()

set(FIRMWARE_FILE pico9918${PICO9918_BINARY_SUFFIX})



if(PICO9918_BUILD_COMBINED)
    include(ExternalProject)
    find_package(Python3 REQUIRED)

    set(COMMON_CMAKE_ARGS
        -DPICO_BOARD_HEADER_DIRS=${CMAKE_SOURCE_DIR}/src/boards
        -DPICO9918_SCART_RGBS=${PICO9918_SCART_RGBS}
        -DPICO9918_SCART_PAL=${PICO9918_SCART_PAL}
        -DPICO9918_SCART_AUTODETECT=${PICO9918_SCART_AUTODETECT}
        -DPICO9918_NO_SPLASH=${PICO9918_NO_SPLASH}
        -DPICO9918_DIAG=${PICO9918_DIAG}
        -DPICO_SDK_FETCH_FROM_GIT=${PICO_SDK_FETCH_FROM_GIT}
        -DPICO_SDK_FETCH_FROM_GIT_TAG=${PICO_SDK_FETCH_FROM_GIT_TAG}
    )
    if(WIN32)
        list(APPEND COMMON_CMAKE_ARGS
            -DCMAKE_C_COMPILER_WORKS=1
            -DCMAKE_CXX_COMPILER_WORKS=1
        )
    endif()

    ExternalProject_Add(build_pico9918
        SOURCE_DIR ${CMAKE_SOURCE_DIR}
        BINARY_DIR ${CMAKE_BINARY_DIR}/pico9918
        CMAKE_ARGS ${COMMON_CMAKE_ARGS} -DPICO_BOARD=pico9918
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --target firmware
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
    )

    ExternalProject_Add(build_pico9918pro
        SOURCE_DIR ${CMAKE_SOURCE_DIR}
        BINARY_DIR ${CMAKE_BINARY_DIR}/pico9918pro
        CMAKE_ARGS ${COMMON_CMAKE_ARGS} -DPICO_BOARD=pico9918pro
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --target firmware
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
    )

    # Compute expected firmware filenames (mirrors logic in src/CMakeLists.txt)
    set(RP2040_SUFFIX "-${PICO9918_OUTPUT_STR}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR}")
    set(RP2350_SUFFIX "pro-${PICO9918_OUTPUT_STR}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR}")
    if(PICO9918_DIAG)
        set(RP2040_SUFFIX "${RP2040_SUFFIX}-diag")
        set(RP2350_SUFFIX "${RP2350_SUFFIX}-diag")
    endif()

    set(UF2_RP2040  ${CMAKE_BINARY_DIR}/pico9918/dist/pico9918${RP2040_SUFFIX}.uf2)
    set(UF2_RP2350  ${CMAKE_BINARY_DIR}/pico9918pro/dist/pico9918${RP2350_SUFFIX}.uf2)
    set(UF2_COMBINED ${CMAKE_BINARY_DIR}/dist/pico9918${PICO9918_OUTPUT_PREFIX}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR}.uf2)

    add_custom_target(combined ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/concat_uf2.py
                ${UF2_RP2040} ${UF2_RP2350} ${UF2_COMBINED}
        DEPENDS build_pico9918 build_pico9918pro
        COMMENT "Creating combined UF2: pico9918${PICO9918_OUTPUT_PREFIX}-v${PICO9918_VERSION_STR}.uf2"
    )

    # Build configurator ROMs using the combined UF2
    ExternalProject_Add(build_configurators
        SOURCE_DIR ${CMAKE_SOURCE_DIR}
        BINARY_DIR ${CMAKE_BINARY_DIR}/configurators
        CMAKE_ARGS
            -DCONFIGURATOR_ONLY=ON
            -DPICO9918_FIRMWARE_UF2_PATH=${UF2_COMBINED}
            -DPICO9918_VERSION=${PICO9918_VERSION}
            -DPICO9918_VERSION_STR=${PICO9918_VERSION_STR}
            -DPICO9918_MAJOR_VER=${PICO9918_MAJOR_VER}
            -DPICO9918_MINOR_VER=${PICO9918_MINOR_VER}
            -DPICO9918_PATCH_VER=${PICO9918_PATCH_VER}
            -DPICO9918_OUTPUT_STR=${PICO9918_OUTPUT_STR}
            -DPICO9918_OUTPUT_PREFIX=${PICO9918_OUTPUT_PREFIX}
            -DPICO9918_GIT_BRANCH=${PICO9918_GIT_BRANCH}
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --target configurator_all
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
        DEPENDS combined
    )

elseif(CONFIGURATOR_ONLY)
    add_subdirectory(configtool)
else()
    # Initialise the Raspberry Pi Pico SDK
    pico_sdk_init()

    add_subdirectory(submodules/vrEmuTms9918)
    add_subdirectory(src)
    add_subdirectory(test)
    add_subdirectory(configtool)
endif()