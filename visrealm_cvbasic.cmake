cmake_minimum_required(VERSION 3.12)

# CVBasic build functions for PICO9918 Configurator

include(ExternalProject)
# ${PYTHON} comes from pico9918_common.cmake.

# Setup CVBasic toolchain - either by finding existing tools or building from source
#
# Version control:
# Use cmake cache variables to specify tool versions:
#   -DCVBASIC_GIT_TAG=v1.2.3    (default: master)
#   -DGASM80_GIT_TAG=v0.9.1     (default: master)
#   -DXDT99_GIT_TAG=3.5.0       (default: master)
#
# Examples:
#   cmake .. -DCVBASIC_GIT_TAG=v1.2.3
#   cmake .. -DGASM80_GIT_TAG=v0.9.1 -DXDT99_GIT_TAG=3.5.0
#
function(setup_cvbasic_tools)
    option(BUILD_TOOLS_FROM_SOURCE "Build CVBasic, gasm80 and XDT99 from source" ON)

    # Tool version/tag configuration
    set(CVBASIC_GIT_TAG "5947e26c86af34e3270f1ecaf85d8036f12c3658" CACHE STRING "CVBasic git tag/branch/commit")
    set(GASM80_GIT_TAG "master" CACHE STRING "GASM80 git tag/branch/commit")
    set(XDT99_GIT_TAG "master" CACHE STRING "XDT99 git tag/branch/commit")
    
    if(BUILD_TOOLS_FROM_SOURCE)
        # Use system default compilers for host builds
        if(WIN32)
            # On Windows, let CMake find the default system compiler
            set(HOST_CMAKE_ARGS "")
        else()
            # On Unix, explicitly specify common compiler paths
            set(HOST_CMAKE_ARGS 
                "-DCMAKE_C_COMPILER=gcc"
                "-DCMAKE_CXX_COMPILER=g++"
            )
        endif()
        
        # Build CVBasic from visrealm fork using separate process to avoid cross-compilation issues
        ExternalProject_Add(CVBasic_external
            GIT_REPOSITORY https://github.com/visrealm/CVBasic.git
            GIT_TAG ${CVBASIC_GIT_TAG}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND
                ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/external/CVBasic/bin &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
                    ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/CVBasic -B build &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
                    ${CMAKE_COMMAND} --build build --config Release &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
                    ${CMAKE_COMMAND} --install build --config Release &&
                ${CMAKE_COMMAND} -E copy_if_different <SOURCE_DIR>/linkticart.py ${CMAKE_BINARY_DIR}/external/CVBasic/
        )
        
        # Build gasm80 from visrealm fork using separate process to avoid cross-compilation issues
        ExternalProject_Add(gasm80_external
            GIT_REPOSITORY https://github.com/visrealm/gasm80.git
            GIT_TAG ${GASM80_GIT_TAG}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND 
                ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/external/gasm80/bin &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> 
                    ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external/gasm80 -B build &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> 
                    ${CMAKE_COMMAND} --build build --config Release &&
                ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
                    ${CMAKE_COMMAND} --install build --config Release
        )
        
        # Build XDT99 tools (Python-based)
        ExternalProject_Add(XDT99_external
            GIT_REPOSITORY https://github.com/endlos99/xdt99.git
            GIT_TAG ${XDT99_GIT_TAG}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND 
                ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR> ${CMAKE_BINARY_DIR}/external/xdt99
        )
        
        # Set tool paths for external builds
        set(CVBASIC_EXE "${CMAKE_BINARY_DIR}/external/CVBasic/bin/cvbasic" PARENT_SCOPE)
        set(GASM80_EXE "${CMAKE_BINARY_DIR}/external/gasm80/bin/gasm80" PARENT_SCOPE)
        set(XAS99_SCRIPT "${CMAKE_BINARY_DIR}/external/xdt99/xas99.py" PARENT_SCOPE)
        set(LINKTICART_SCRIPT "${CMAKE_BINARY_DIR}/external/CVBasic/linkticart.py" PARENT_SCOPE)
        
        # Add dependencies to all CVBasic targets
        set(TOOL_DEPENDENCIES CVBasic_external gasm80_external XDT99_external PARENT_SCOPE)

        message(STATUS "CVBasic tools will be built from source")
        message(STATUS "CVBasic version/tag: ${CVBASIC_GIT_TAG}")
        message(STATUS "GASM80 version/tag: ${GASM80_GIT_TAG}")
        message(STATUS "XDT99 version/tag: ${XDT99_GIT_TAG}")

    else()
        # Find required tools (original behavior)
        find_program(CVBASIC_EXE cvbasic PATHS ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic ${CMAKE_SOURCE_DIR}/../CVBasic/build/Release REQUIRED)
        find_program(GASM80_EXE gasm80 PATHS ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic REQUIRED)

        # Find linkticart.py in local CVBasic installation or fallback to bundled version
        find_file(LINKTICART_SCRIPT linkticart.py
            PATHS
                ${CMAKE_SOURCE_DIR}/../CVBasic
                ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic
            DOC "CVBasic linkticart.py script"
        )
        if(NOT LINKTICART_SCRIPT)
            set(LINKTICART_SCRIPT "${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic/linkticart.py")
        endif()
        
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
        
        set(TOOL_DEPENDENCIES "" PARENT_SCOPE)

        message(STATUS "Using existing CVBasic tools")
        message(STATUS "CVBasic: ${CVBASIC_EXE}")
        message(STATUS "GASM80: ${GASM80_EXE}")
        message(STATUS "linkticart.py: ${LINKTICART_SCRIPT}")
        if(XAS99_SCRIPT)
            message(STATUS "XAS99: ${XAS99_SCRIPT}")
        else()
            message(STATUS "XAS99: NOT FOUND (TI-99 builds will be limited)")
        endif()
    endif()
endfunction()

# Generate a small Python helper that resolves the XAS99 bank file and calls linkticart.
# Written to the build directory at configure time.
function(_ensure_xas99_bank_resolver)
    if(XAS99_RESOLVE_AND_LINK)
        return()
    endif()
    set(SCRIPT_PATH "${CMAKE_BINARY_DIR}/resolve_xas99_bank.py")
    file(WRITE "${SCRIPT_PATH}" [=[
"""Resolve XAS99 banked output filename and invoke linkticart.py"""
import glob, subprocess, sys

base, linkticart, output, title = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
matches = sorted(glob.glob(base + '_b0*.bin'))
if not matches:
    print(f'Error: no bank file found matching {base}_b0*.bin', file=sys.stderr)
    sys.exit(1)
print(f'Using bank file: {matches[0]}')
sys.exit(subprocess.call([sys.executable, linkticart, matches[0], output, title]))
]=])
    set(XAS99_RESOLVE_AND_LINK "${SCRIPT_PATH}" PARENT_SCOPE)
endfunction()