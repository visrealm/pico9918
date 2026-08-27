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
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Optional, git-ignored local build config. If present, it can set or override
# any CMake variable/option used below (e.g. PICO_BOARD, PICO9918_* options).
# Absent by default - the build works unchanged without it. See
# pico9918_config.cmake.template for the available overrides.
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico9918_config.cmake)
    message(STATUS "Using local build config: pico9918_config.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/pico9918_config.cmake)
endif()

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards )
# Default board, applied only if not already set (e.g. by pico9918_config.cmake).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico9918pro CACHE STRING "Board type")
endif()
#set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")

option(PICO9918_BUILD_COMBINED "Build combined PICO9918 + PICO9918 PRO UF2" OFF)

# Check if building configurator only (skip Pico SDK setup)
if(NOT CONFIGURATOR_ONLY AND NOT PICO9918_BUILD_COMBINED)
    # Pull in Raspberry Pi Pico SDK (must be before project)
    include(pico_sdk_import.cmake)
endif()

# PICO9918 Version Information
# Each default is applied only if not already set (e.g. by pico9918_config.cmake).
if(NOT DEFINED PICO9918_MAJOR_VER)
    set(PICO9918_MAJOR_VER 1)
endif()
if(NOT DEFINED PICO9918_MINOR_VER)
    set(PICO9918_MINOR_VER 3)
endif()
if(NOT DEFINED PICO9918_PATCH_VER)
    set(PICO9918_PATCH_VER 0)
endif()
# Optional artifact suffix (e.g. "a1", "alpha1", "rc2"). When non-empty, this
# overrides the git branch name in generated artifact filenames. Setting it to an
# empty string is a valid choice (use the branch name), so we guard on DEFINED.
if(NOT DEFINED PICO9918_VERSION_SUFFIX)
    set(PICO9918_VERSION_SUFFIX "")
endif()

# Common helpers must follow the version vars above and precede visrealm_*.
include(pico9918_common.cmake)
include(visrealm_tools.cmake)
include(visrealm_cvbasic.cmake)

if(PICO9918_BUILD_COMBINED OR CONFIGURATOR_ONLY)
    project(pico9918 NONE)
else()
    project(pico9918 C CXX ASM)
endif()

# PICO9918 Build Options
option(PICO9918_ENABLE_SCART "Enable SCART support" ON)
option(PICO9918_NO_SPLASH "Disable splash screen" OFF)
option(PICO9918_DIAG "Enable diagnostic mode" OFF)
option(PICO9918_GPU_FRAME_COUNTER "Enable GPU frame counter" OFF)
option(PICO9918_COLD_IN_FLASH "Keep boot-only cold code/data resident in flash (XIP) instead of copying to RAM" ON)

# Live test capture: one copy per scanline into a whole-frame buffer a debug
# probe can read over SWD. Test builds only - it costs about a microsecond a
# line, so never take timing readings from a build with it on.
option(PICO9918_LIVE_TEST "Enable the SWD live test capture buffer" OFF)
if(PICO9918_LIVE_TEST)
    add_definitions(-DPICO9918_LIVE_TEST=1)
endif()

# Custom-hardware behavioural flags (see pico9918_config.cmake). Each emits a
# define only when enabled, so the C code guards with #ifdef.
option(PICO9918_NO_CLOCKS "Disable GROMCLK/CPUCLK clock outputs" OFF)
option(PICO9918_INT_ACTIVE_HIGH "Drive /INT active-high instead of active-low" OFF)
if(PICO9918_NO_CLOCKS)
    add_definitions(-DPICO9918_NO_CLOCKS)
endif()
if(PICO9918_INT_ACTIVE_HIGH)
    add_definitions(-DPICO9918_INT_ACTIVE_HIGH)
endif()

add_definitions(-DPICO_BUILD=1)
add_definitions(-DPICO9918_SINGLE_INSTANCE=1)
add_definitions(-DPICO9918_MODE=1)

# Also expose mode as a CMake variable for the library's CMakeLists.txt
set(PICO9918_MODE 1 CACHE STRING "TMS9918 emulation mode (0=TMS9918, 1=F18A; V9938 is additive via PICO9918_V9938_BASE)")

# 80-column text at eight bits a pixel. A board capability rather than a chip, so it stays
# orthogonal to PICO9918_MODE: it buys the tile palette select, ECM, the bitmap
# layer and the shared composite there, none of which four bits a pixel can represent. It costs a
# 512-byte line and twice the palette expansion, which only the RP2350 has the budget for.
if(PICO_PLATFORM STREQUAL "rp2350" OR PICO_BOARD STREQUAL "pico9918pro")
    set(PICO9918_TEXT80_8BPP_DEFAULT ON)
else()
    set(PICO9918_TEXT80_8BPP_DEFAULT OFF)
endif()
# The option itself is core's, so that a standalone consumer of the published library can
# set it too. Seeding the cache before add_subdirectory is what makes the board the
# default here without a second option() shadowing it.
set(PICO9918_TEXT80_8BPP ${PICO9918_TEXT80_8BPP_DEFAULT} CACHE BOOL
    "80-column text at 8bpp: ECM, palette select and bitmap layer in T80")

# Generated, so PROJECT_NUMBER comes from the version above rather than being a
# second place to bump. The library's Doxyfile is templated the same way.
configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)

# API docs: `cmake --build <dir> --target docs`. Optional - the target only
# exists when doxygen is installed, and is never part of `all`.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    # Doxygen resolves its paths against the working directory, which is why this runs
    # from the source root rather than from the build tree the Doxyfile sits in.
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/build/doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation into build/doxygen/html"
        VERBATIM)
endif()


# Boot clock is VGA 252 MHz. When SCART is detected at runtime, we re-clock to
# 270 MHz (a multiple of 13.5 MHz) for exact integer pioClocksPerPixel.
# Each default is applied only if not already set (e.g. by pico9918_config.cmake).
# These are timing-critical and interdependent - change them together.
if(NOT DEFINED PICO9918_SYS_CLK_HZ)
    set(PICO9918_SYS_CLK_HZ 252000000)
endif()
if(NOT DEFINED PICO9918_PLL_SYS_VCO_FREQ_HZ)
    set(PICO9918_PLL_SYS_VCO_FREQ_HZ 1512000000)
endif()
if(NOT DEFINED PICO9918_PLL_SYS_POSTDIV1)
    set(PICO9918_PLL_SYS_POSTDIV1 6)
endif()
if(NOT DEFINED PICO9918_PLL_SYS_POSTDIV2)
    set(PICO9918_PLL_SYS_POSTDIV2 1)
endif()
add_definitions(-DSYS_CLK_HZ=${PICO9918_SYS_CLK_HZ})
add_definitions(-DPLL_SYS_VCO_FREQ_HZ=${PICO9918_PLL_SYS_VCO_FREQ_HZ})
add_definitions(-DPLL_SYS_POSTDIV1=${PICO9918_PLL_SYS_POSTDIV1})
add_definitions(-DPLL_SYS_POSTDIV2=${PICO9918_PLL_SYS_POSTDIV2})

# SDK Flash write routines exit XIP mode and use boot2 to reenter it, so
# boot2 needs to be properly configured for whatever speed we end up at.
# PICO_FLASH_SPI_CLKDIV could be 2, but then we can't write to firmware at higher clocks.
if(NOT DEFINED PICO9918_FLASH_SPI_CLKDIV)
    set(PICO9918_FLASH_SPI_CLKDIV 4)
endif()
if(NOT DEFINED PICO9918_XOSC_STARTUP_DELAY_MULTIPLIER)
    set(PICO9918_XOSC_STARTUP_DELAY_MULTIPLIER 8)
endif()
add_definitions(-DPICO_FLASH_SPI_CLKDIV=${PICO9918_FLASH_SPI_CLKDIV})
add_definitions(-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=${PICO9918_XOSC_STARTUP_DELAY_MULTIPLIER})


# GPIO / VGA pin assignments. Each is a base pin used by the corresponding
# header default (src/gpio.h, src/vga/vga.h) via #ifndef, so setting one here
# (e.g. in pico9918_config.cmake) overrides the hardware default. Each entry is
# "PICO9918_<NAME> <default>"; the resulting define is -DPICO9918_<NAME>=<value>.
set(PICO9918_PIN_DEFAULTS
    "GPIO_CD7 14"          # CPU data bus base (CD7..CD0 = base..base+7)
    "GPIO_MODE 28"         # mode select
    "GPIO_MODE1 29"        # mode select 1
    "GPIO_INT 22"          # /INT output
    "GPIO_RESET 23"        # soft reset input (v0.4+)
    "GPIO_GROMCL 25"       # GROM clock (v0.4+)
    "GPIO_CPUCL 24"        # CPU clock (v0.4+)
    "GPIO_GROMCL_V03 29"   # GROM clock (v0.3 hardware)
    "GPIO_CPUCL_V03 23"    # CPU clock (v0.3 hardware)
    "VGA_SYNC_PINS_START 0" # first VGA sync pin (h, v = base, base+1)
    "VGA_RGB_PINS_START 2"  # first VGA rgb pin (12 pins from base)
)
foreach(_entry ${PICO9918_PIN_DEFAULTS})
    separate_arguments(_parts UNIX_COMMAND "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _default)
    if(NOT DEFINED PICO9918_${_name})
        set(PICO9918_${_name} ${_default})
    endif()
    add_definitions(-DPICO9918_${_name}=${PICO9918_${_name}})
endforeach()

# Library-facing alias for the /INT pin. The tier-1 PICO9918_HOST_SET_INT op lives in
# the library's pico platform header, which carries no PICO9918_* names and has no
# fallback pin - it #errors if this is missing rather than guessing one.
add_definitions(-DPICO9918_INT_GPIO=${PICO9918_GPIO_INT})

# The firmware's tier-1 host ops header. The library's platform dispatch header
# includes this by name, so the ENTER/EXIT_CRITICAL and STATUS_VISIBLE macros expand
# inside the library TUs that use them - no call and no function pointer on the
# per-active-scanline path.
add_definitions(-DPICO9918_HOST_OPS_HEADER="pico9918HostOps.h")

# Include paths the ops header needs, applied globally because library TUs resolve
# it too: src/ for the header itself and gpio.h, and the binary src/ for the
# pico_generate_pio_header output (tms9918.pio.h) that gpio.h includes.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/src)


# Output-mode suffix: "" for VGA+SCART, "-vgaonly" otherwise.
if(NOT CONFIGURATOR_ONLY)
  if(PICO9918_ENABLE_SCART)
    set(PICO9918_OUTPUT_STR "")
  else()
    set(PICO9918_OUTPUT_STR "-vgaonly")
  endif()
endif()

pico9918_compute_binary_suffix("${PICO_BOARD}" "${PICO9918_OUTPUT_STR}" ${PICO9918_DIAG} PICO9918_BINARY_SUFFIX)
set(FIRMWARE_FILE pico9918${PICO9918_BINARY_SUFFIX})



if(PICO9918_BUILD_COMBINED)
    include(ExternalProject)

    set(COMMON_CMAKE_ARGS
        -DPICO_BOARD_HEADER_DIRS=${CMAKE_SOURCE_DIR}/src/boards
        -DPICO9918_ENABLE_SCART=${PICO9918_ENABLE_SCART}
        -DPICO9918_NO_SPLASH=${PICO9918_NO_SPLASH}
        -DPICO9918_DIAG=${PICO9918_DIAG}
        -DPICO9918_GPU_FRAME_COUNTER=${PICO9918_GPU_FRAME_COUNTER}
        -DPICO9918_LIVE_TEST=${PICO9918_LIVE_TEST}
        -DPICO9918_COLD_IN_FLASH=${PICO9918_COLD_IN_FLASH}
        -DPICO9918_NO_CLOCKS=${PICO9918_NO_CLOCKS}
        -DPICO9918_INT_ACTIVE_HIGH=${PICO9918_INT_ACTIVE_HIGH}
        -DPICO9918_VERSION_SUFFIX=${PICO9918_VERSION_SUFFIX}
        -DPICO_SDK_FETCH_FROM_GIT=${PICO_SDK_FETCH_FROM_GIT}
        -DPICO_SDK_FETCH_FROM_GIT_TAG=${PICO_SDK_FETCH_FROM_GIT_TAG}
    )
    if(PICO_SDK_PATH)
        list(APPEND COMMON_CMAKE_ARGS -DPICO_SDK_PATH=${PICO_SDK_PATH})
    endif()
    if(PICO_TOOLCHAIN_PATH)
        list(APPEND COMMON_CMAKE_ARGS -DPICO_TOOLCHAIN_PATH=${PICO_TOOLCHAIN_PATH})
    endif()
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

    pico9918_compute_binary_suffix("pico9918"    "${PICO9918_OUTPUT_STR}" ${PICO9918_DIAG} RP2040_SUFFIX)
    pico9918_compute_binary_suffix("pico9918pro" "${PICO9918_OUTPUT_STR}" ${PICO9918_DIAG} RP2350_SUFFIX)

    set(UF2_RP2040   ${CMAKE_BINARY_DIR}/pico9918/dist/pico9918${RP2040_SUFFIX}.uf2)
    set(UF2_RP2350   ${CMAKE_BINARY_DIR}/pico9918pro/dist/pico9918${RP2350_SUFFIX}.uf2)
    set(UF2_COMBINED ${CMAKE_BINARY_DIR}/dist/pico9918${PICO9918_OUTPUT_STR}-v${PICO9918_VERSION_STR}${PICO9918_BRANCH_STR}.uf2)

    add_custom_target(combined ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist
        COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/concat_uf2.py
                ${UF2_RP2040} ${UF2_RP2350} ${UF2_COMBINED}
        DEPENDS build_pico9918 build_pico9918pro
        COMMENT "Creating combined UF2: pico9918${PICO9918_OUTPUT_STR}-v${PICO9918_VERSION_STR}.uf2"
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
            -DPICO9918_GIT_BRANCH=${PICO9918_GIT_BRANCH}
            -DPICO9918_VERSION_SUFFIX=${PICO9918_VERSION_SUFFIX}
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

    # Library inputs the firmware owns, set before the library is added. The
    # diag overlay's GPU-frames row is one: the count is host-pushed, so the row
    # and its setter must be gated by the same option that produces it.
    set(PICO9918_DIAG_GPU_FRAME_COUNTER ${PICO9918_GPU_FRAME_COUNTER} CACHE BOOL
        "Diag GPU-frames row (forwarded from PICO9918_GPU_FRAME_COUNTER)" FORCE)
    # The splash image is board-conditional and the assets live in the library.
    if(PICO_BOARD STREQUAL "pico9918pro")
        set(PICO9918_SPLASH_IMAGE res/splash_pro.png)
    else()
        set(PICO9918_SPLASH_IMAGE res/splash.png)
    endif()

    add_subdirectory(core)

    # The library carries its own version, because a published library must. It is
    # generated from this repository, so the two are one release - and a silent
    # divergence would ship a package whose version means nothing.
    set(PICO9918_CORE_EXPECTED_VER
        "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
    if(NOT pico9918_core_VERSION VERSION_EQUAL PICO9918_CORE_EXPECTED_VER)
        message(WARNING "core/CMakeLists.txt says ${pico9918_core_VERSION}, this firmware is "
                        "${PICO9918_CORE_EXPECTED_VER} - bump the library's project() version too")
    endif()

    add_subdirectory(src)
    add_subdirectory(test)
    add_subdirectory(configtool)
endif()