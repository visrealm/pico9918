cmake_minimum_required(VERSION 3.13)

# PICO9918 Configurator CMake Build
# Converts the batch-based build to cross-platform CMake

project(pico9918-configurator)

include(ExternalProject)

set(PYTHON python3)

# Version information
set(VERSION "v1-0-2")
string(REPLACE "-" "." FRIENDLYVER ${VERSION})

# External tool dependencies
option(BUILD_TOOLS_FROM_SOURCE "Build CVBasic, gasm80 and XDT99 from source" OFF)

if(BUILD_TOOLS_FROM_SOURCE)
    # Build CVBasic from visrealm fork
    ExternalProject_Add(CVBasic_external
        GIT_REPOSITORY https://github.com/visrealm/CVBasic.git
        GIT_TAG master
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/CVBasic
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release
        INSTALL_COMMAND ${CMAKE_COMMAND} --install . --config Release
    )
    
    # Build gasm80 from visrealm fork
    ExternalProject_Add(gasm80_external
        GIT_REPOSITORY https://github.com/visrealm/gasm80.git
        GIT_TAG master
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/gasm80
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release
        INSTALL_COMMAND ${CMAKE_COMMAND} --install . --config Release
    )
    
    # Build XDT99 tools (Python-based)
    ExternalProject_Add(XDT99_external
        GIT_REPOSITORY https://github.com/endlos99/xdt99.git
        GIT_TAG master
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND 
            ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR> ${CMAKE_BINARY_DIR}/external/xdt99
    )
    
    # Set tool paths for external builds
    set(CVBASIC_EXE "${CMAKE_BINARY_DIR}/external/CVBasic/bin/cvbasic${CMAKE_EXECUTABLE_SUFFIX}")
    set(GASM80_EXE "${CMAKE_BINARY_DIR}/external/gasm80/bin/gasm80${CMAKE_EXECUTABLE_SUFFIX}")
    set(XAS99_SCRIPT "${CMAKE_BINARY_DIR}/external/xdt99/xas99.py")
    
    # Add dependencies to all CVBasic targets
    set(TOOL_DEPENDENCIES CVBasic_external gasm80_external XDT99_external)
else()
    # Find required tools (original behavior)
    find_program(CVBASIC_EXE cvbasic PATHS tools/cvbasic ${CMAKE_SOURCE_DIR}/../CVBasic/build/Release REQUIRED)
    find_program(GASM80_EXE gasm80 PATHS tools/cvbasic REQUIRED)
    
    # Platform-specific tool paths
    if(WIN32)
        find_program(XAS99_SCRIPT xas99.py PATHS c:/tools/xdt99)
        if(NOT XAS99_SCRIPT)
            message(WARNING "XAS99 not found, TI-99 builds will be skipped")
        endif()
    else()
        find_program(XAS99_SCRIPT xas99.py PATHS /usr/local/bin /opt/xdt99)
        if(NOT XAS99_SCRIPT)
            message(WARNING "XAS99 not found, TI-99 builds will be skipped")
        endif()
    endif()
    
    set(TOOL_DEPENDENCIES)
endif()

# Firmware dependency
set(FIRMWARE_FILE "${CMAKE_SOURCE_DIR}/../build/src/pico9918-vga-build-${VERSION}.uf2")
if(NOT EXISTS "${FIRMWARE_FILE}")
    message(FATAL_ERROR "Firmware file not found: ${FIRMWARE_FILE}. Please build main firmware first.")
endif()

# Custom function to convert UF2 to CVBasic data
function(convert_uf2_to_cvbasic BANK_SIZE OUTPUT_PREFIX)
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/intermediate")
    add_custom_command(
        OUTPUT "${OUTPUT_DIR}/${OUTPUT_PREFIX}.h.bas" "${OUTPUT_DIR}/${OUTPUT_PREFIX}.bas"
        COMMAND ${PYTHON} "${CMAKE_SOURCE_DIR}/tools/uf2cvb.py" 
                -b ${BANK_SIZE} 
                -o "${OUTPUT_DIR}/${OUTPUT_PREFIX}" 
                "${FIRMWARE_FILE}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/uf2cvb.py" "${FIRMWARE_FILE}"
        COMMENT "Converting UF2 to CVBasic data (${BANK_SIZE}KB banks)"
        VERBATIM
    )
endfunction()

# Generate firmware data files
convert_uf2_to_cvbasic(8 "firmware_8k")
convert_uf2_to_cvbasic(16 "firmware_16k") 
convert_uf2_to_cvbasic(0 "firmware")

# Custom function to compile CVBasic for a platform
function(add_cvbasic_target TARGET_NAME PLATFORM_FLAG DEFINES ASM_OUTPUT ROM_OUTPUT DESCRIPTION)
    set(SOURCE_DIR "${CMAKE_BINARY_DIR}/intermediate/src")
    set(ASM_DIR "${CMAKE_BINARY_DIR}/intermediate/asm")
    set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
    
    # Create directories
    file(MAKE_DIRECTORY "${SOURCE_DIR}")
    file(MAKE_DIRECTORY "${ASM_DIR}")
    file(MAKE_DIRECTORY "${DIST_DIR}")
    
    # Copy source files and generated firmware data
    add_custom_command(
        OUTPUT "${SOURCE_DIR}/pico9918conf.bas"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src" "${SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.h.bas" "${SOURCE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.bas" "${SOURCE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.h.bas" "${SOURCE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.bas" "${SOURCE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware.h.bas" "${SOURCE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/intermediate/firmware.bas" "${SOURCE_DIR}/"
        DEPENDS ${CMAKE_SOURCE_DIR}/src/pico9918conf.bas 
                "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.h.bas" "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.bas"
                "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.h.bas" "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.bas"
                "${CMAKE_BINARY_DIR}/intermediate/firmware.h.bas" "${CMAKE_BINARY_DIR}/intermediate/firmware.bas"
        COMMENT "Copying CVBasic source files and firmware data"
    )
    
    # Platform-specific dependencies
    set(FIRMWARE_DEPS)
    if("${PLATFORM_FLAG}" STREQUAL "--ti994a")
        list(APPEND FIRMWARE_DEPS "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.h.bas" "${CMAKE_BINARY_DIR}/intermediate/firmware_8k.bas")
    else()
        list(APPEND FIRMWARE_DEPS "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.h.bas" "${CMAKE_BINARY_DIR}/intermediate/firmware_16k.bas")
    endif()
    
    # CVBasic compilation
    set(CVBASIC_COMMAND ${CVBASIC_EXE} ${PLATFORM_FLAG})
    if(DEFINES)
        list(APPEND CVBASIC_COMMAND ${DEFINES})
    endif()
    list(APPEND CVBASIC_COMMAND "pico9918conf.bas" "${ASM_DIR}/${ASM_OUTPUT}" "${CMAKE_SOURCE_DIR}/src/lib")
    
    add_custom_command(
        OUTPUT "${ASM_DIR}/${ASM_OUTPUT}"
        COMMAND ${CVBASIC_COMMAND}
        DEPENDS "${SOURCE_DIR}/pico9918conf.bas" ${FIRMWARE_DEPS} ${TOOL_DEPENDENCIES}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        COMMENT "Compiling CVBasic for ${DESCRIPTION}"
        VERBATIM
    )
    
    # Assembly step (platform specific)
    if("${PLATFORM_FLAG}" STREQUAL "--ti994a")
        if(XAS99_SCRIPT)
            # XAS99 generates files based on the assembly filename, not the target name
            get_filename_component(ASM_NAME "${ASM_OUTPUT}" NAME_WE)
            add_custom_command(
                OUTPUT "${DIST_DIR}/${ROM_OUTPUT}"
                COMMAND ${PYTHON} "${XAS99_SCRIPT}" -b -R "${ASM_DIR}/${ASM_OUTPUT}"
                COMMAND ${PYTHON} "${CMAKE_SOURCE_DIR}/tools/cvbasic/linkticart.py" 
                        "${ASM_DIR}/${ASM_NAME}_b00.bin" 
                        "${DIST_DIR}/${ROM_OUTPUT}" 
                        "PICO9918 ${FRIENDLYVER}"
                DEPENDS "${ASM_DIR}/${ASM_OUTPUT}"
                WORKING_DIRECTORY "${ASM_DIR}"
                COMMENT "Assembling ${DESCRIPTION}"
                VERBATIM
            )
        else()
            # Create a dummy target when XAS99 is not available
            add_custom_command(
                OUTPUT "${DIST_DIR}/${ROM_OUTPUT}"
                COMMAND ${CMAKE_COMMAND} -E echo "XAS99 not available, skipping ${DESCRIPTION}"
                COMMAND ${CMAKE_COMMAND} -E touch "${DIST_DIR}/${ROM_OUTPUT}"
                DEPENDS "${ASM_DIR}/${ASM_OUTPUT}"
                COMMENT "Skipping ${DESCRIPTION} (XAS99 not found)"
            )
        endif()
    else()
        add_custom_command(
            OUTPUT "${DIST_DIR}/${ROM_OUTPUT}"
            COMMAND ${GASM80_EXE} "${ASM_DIR}/${ASM_OUTPUT}" -o "${DIST_DIR}/${ROM_OUTPUT}"
            DEPENDS "${ASM_DIR}/${ASM_OUTPUT}" ${TOOL_DEPENDENCIES}
            WORKING_DIRECTORY "${ASM_DIR}"
            COMMENT "Assembling ${DESCRIPTION}"
            VERBATIM
        )
    endif()
    
    add_custom_target(${TARGET_NAME} DEPENDS "${DIST_DIR}/${ROM_OUTPUT}")
endfunction()

# Define all platform targets
add_cvbasic_target(ti99 "--ti994a" "" "pico9918_${VERSION}_ti99.a99" "pico9918_${VERSION}_ti99_8.bin" "TI-99/4A")
add_cvbasic_target(ti99_f18a "--ti994a" "-dF18A_TESTING=1" "pico9918_${VERSION}_ti99_f18a.a99" "pico9918_${VERSION}_ti99_f18a_8.bin" "TI-99/4A F18A Testing")
add_cvbasic_target(coleco "" "" "pico9918_${VERSION}_cv.asm" "pico9918_${VERSION}_cv.rom" "ColecoVision")
add_cvbasic_target(msx_asc16 "--msx" "" "pico9918_${VERSION}_msx_asc16.asm" "pico9918_${VERSION}_msx_asc16.rom" "MSX ASCII16")
add_cvbasic_target(msx_konami "--msx" "-konami" "pico9918_${VERSION}_msx_konami.asm" "pico9918_${VERSION}_msx_konami.rom" "MSX Konami")
add_cvbasic_target(nabu "--nabu" "" "pico9918_${VERSION}_nabu.asm" "pico9918_${VERSION}.nabu" "NABU")
add_cvbasic_target(creativision "--creativision" "" "pico9918_${VERSION}_crv.asm" "pico9918_${VERSION}_crv.bin" "CreatiVision")

# Special NABU MAME target (different defines)
add_cvbasic_target(nabu_mame "--nabu" "-DTMS9918_TESTING=1" "pico9918_${VERSION}_nabu_mame.asm" "000001.nabu" "NABU MAME")

# NABU MAME packaging
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.npz"
    COMMAND ${CMAKE_COMMAND} -E tar "cf" "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.zip" --format=zip "000001.nabu"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.zip" "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.npz"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.zip" "000001.nabu"
    DEPENDS "${CMAKE_BINARY_DIR}/dist/000001.nabu"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    COMMENT "Packaging NABU MAME"
    VERBATIM
)

add_custom_target(nabu_mame_package DEPENDS "${CMAKE_BINARY_DIR}/dist/pico9918_${VERSION}_nabu_mame.npz")

# Master target to build all platforms
add_custom_target(configurator_all DEPENDS ti99 ti99_f18a coleco msx_asc16 msx_konami nabu creativision nabu_mame_package)

# Print build information
message(STATUS "PICO9918 Configurator CMake Configuration")
message(STATUS "Version: ${FRIENDLYVER}")
message(STATUS "CVBasic: ${CVBASIC_EXE}")
message(STATUS "GASM80: ${GASM80_EXE}")
if(XAS99_SCRIPT)
    message(STATUS "XAS99: ${XAS99_SCRIPT}")
else()
    message(STATUS "XAS99: NOT FOUND (TI-99 builds will be limited)")
endif()
message(STATUS "Firmware: ${FIRMWARE_FILE}")