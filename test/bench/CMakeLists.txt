cmake_minimum_required(VERSION 3.13)

# PICO9918 render benchmark ROM.
#
# Standalone: it needs only the CVBasic toolchain, which the configurator build
# has already fetched and built. Configure with
#
#   cmake -S test/bench -B build-bench -G Ninja
#   cmake --build build-bench
#
# and the ROMs land in build-bench/dist. Point PICO9918_TOOLS_DIR at another
# firmware build tree if this one has not built the configurator yet.

project(pico9918-bench NONE)

set(PICO9918_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/pico9918/external"
    CACHE PATH "Directory holding the CVBasic, gasm80 and xdt99 tool builds")

find_program(CVBASIC_EXE cvbasic PATHS "${PICO9918_TOOLS_DIR}/CVBasic/bin" NO_DEFAULT_PATH)
find_program(GASM80_EXE gasm80 PATHS "${PICO9918_TOOLS_DIR}/gasm80/bin" NO_DEFAULT_PATH)
find_file(XAS99_SCRIPT xas99.py PATHS "${PICO9918_TOOLS_DIR}/xdt99" NO_DEFAULT_PATH)
find_file(LINKTICART_SCRIPT linkticart.py PATHS "${PICO9918_TOOLS_DIR}/CVBasic" NO_DEFAULT_PATH)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(NOT CVBASIC_EXE OR NOT GASM80_EXE)
    message(FATAL_ERROR
        "CVBasic tools not found under ${PICO9918_TOOLS_DIR}. "
        "Build the configurator once (target configurator_all) to fetch them, "
        "or set -DPICO9918_TOOLS_DIR=<firmware build>/external")
endif()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/bench.bas")
set(LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../configtool/src/lib")
set(DIST "${CMAKE_CURRENT_BINARY_DIR}/dist")
file(MAKE_DIRECTORY "${DIST}")

# Each target compiles in its own directory: run concurrently in a shared one and
# CVBasic crosses the outputs over, so a Z80 target ends up holding 9900 code.
function(bench_asm_dir NAME OUT_VAR)
    set(DIR "${CMAKE_CURRENT_BINARY_DIR}/asm/${NAME}")
    file(MAKE_DIRECTORY "${DIR}")
    set(${OUT_VAR} "${DIR}" PARENT_SCOPE)
endfunction()

# Z80 platforms: cvbasic -> gasm80 -> flat cart image
function(add_bench_z80 NAME PLATFORM_FLAG ASM_NAME ROM_NAME DESCRIPTION)
    bench_asm_dir(${NAME} ASM_DIR)
    set(CMD ${CVBASIC_EXE})
    if(PLATFORM_FLAG)
        list(APPEND CMD ${PLATFORM_FLAG})
    endif()
    add_custom_command(
        OUTPUT "${ASM_DIR}/${ASM_NAME}"
        COMMAND ${CMD} "${SRC}" "${ASM_DIR}/${ASM_NAME}" "${LIB}"
        DEPENDS "${SRC}"
        WORKING_DIRECTORY "${ASM_DIR}"
        COMMENT "Compiling bench for ${DESCRIPTION}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${DIST}/${ROM_NAME}"
        COMMAND ${GASM80_EXE} "${ASM_DIR}/${ASM_NAME}" -o "${DIST}/${ROM_NAME}"
        DEPENDS "${ASM_DIR}/${ASM_NAME}"
        WORKING_DIRECTORY "${ASM_DIR}"
        COMMENT "Assembling bench for ${DESCRIPTION}"
        VERBATIM)
    add_custom_target(${NAME} ALL DEPENDS "${DIST}/${ROM_NAME}")
endfunction()

add_bench_z80(bench_cv  ""      "bench_cv.asm"  "pico9918bench_cv.rom"  "ColecoVision")
add_bench_z80(bench_msx "--msx" "bench_msx.asm" "pico9918bench_msx.rom" "MSX")

# TI-99: cvbasic -> xas99 -> linkticart. Under 32KB needs no banking, so this is
# the flat .bin path, as the configurator's lite build uses.
if(XAS99_SCRIPT AND LINKTICART_SCRIPT)
    bench_asm_dir(bench_ti99 ASM_DIR)
    add_custom_command(
        OUTPUT "${ASM_DIR}/bench_ti99.a99"
        COMMAND ${CVBASIC_EXE} --ti994a "${SRC}" "${ASM_DIR}/bench_ti99.a99" "${LIB}"
        DEPENDS "${SRC}"
        WORKING_DIRECTORY "${ASM_DIR}"
        COMMENT "Compiling bench for TI-99/4A"
        VERBATIM)
    add_custom_command(
        OUTPUT "${DIST}/pico9918bench_ti99_8.bin"
        COMMAND ${Python3_EXECUTABLE} "${XAS99_SCRIPT}" -b -R "${ASM_DIR}/bench_ti99.a99"
        COMMAND ${Python3_EXECUTABLE} "${LINKTICART_SCRIPT}"
                "${ASM_DIR}/bench_ti99.bin" "${DIST}/pico9918bench_ti99_8.bin"
                "PICO9918 BENCH"
        DEPENDS "${ASM_DIR}/bench_ti99.a99"
        WORKING_DIRECTORY "${ASM_DIR}"
        COMMENT "Assembling bench for TI-99/4A"
        VERBATIM)
    add_custom_target(bench_ti99 ALL DEPENDS "${DIST}/pico9918bench_ti99_8.bin")
else()
    message(WARNING "xas99 or linkticart not found - skipping the TI-99 bench ROM")
endif()
