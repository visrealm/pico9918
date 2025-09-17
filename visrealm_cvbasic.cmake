cmake_minimum_required(VERSION 3.12)

# CVBasic build functions for PICO9918 Configurator

include(ExternalProject)
set(PYTHON python3)

# Find CVBasic tools with fallback paths
function(find_cvbasic_tools)
    find_program(CVBASIC_EXECUTABLE cvbasic 
        PATHS 
            ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic
            ${CMAKE_SOURCE_DIR}/../CVBasic/build/Release
            ENV PATH
        DOC "CVBasic compiler executable"
    )
    
    find_program(GASM80_EXECUTABLE gasm80
        PATHS 
            ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic
        DOC "GASM80 assembler executable"  
    )
    
    if(WIN32)
        find_program(XAS99_SCRIPT xas99.py
            PATHS c:/tools/xdt99
            DOC "XDT99 XAS99 assembler script"
        )
    endif()
    
    # Set parent scope variables
    set(CVBASIC_FOUND ${CVBASIC_EXECUTABLE} PARENT_SCOPE)
    set(GASM80_FOUND ${GASM80_EXECUTABLE} PARENT_SCOPE) 
    set(XAS99_FOUND ${XAS99_SCRIPT} PARENT_SCOPE)
    
    if(CVBASIC_EXECUTABLE)
        message(STATUS "Found CVBasic: ${CVBASIC_EXECUTABLE}")
    else()
        message(WARNING "CVBasic not found - configurator builds will fail")
    endif()
    
    if(GASM80_EXECUTABLE)
        message(STATUS "Found GASM80: ${GASM80_EXECUTABLE}")
    else()
        message(WARNING "GASM80 not found - some platform builds will fail")
    endif()
    
    if(XAS99_SCRIPT)
        message(STATUS "Found XAS99: ${XAS99_SCRIPT}")
    else()
        message(STATUS "XAS99 not found - TI-99 builds will be limited")
    endif()
endfunction()

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
    set(CVBASIC_GIT_TAG "master" CACHE STRING "CVBasic git tag/branch/commit")
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

# Convert UF2 firmware to CVBasic data arrays
function(visrealm_uf2_to_cvbasic TARGET UF2_FILE BANK_SIZE OUTPUT_BASE)
    set(UF2_CONV ${CMAKE_SOURCE_DIR}/configtool/tools/uf2cvb.py)
    
    add_custom_command(
        OUTPUT ${OUTPUT_BASE}.h.bas ${OUTPUT_BASE}.bas
        COMMAND ${PYTHON} ${UF2_CONV} -b ${BANK_SIZE} -o ${OUTPUT_BASE} ${UF2_FILE}
        DEPENDS ${UF2_CONV} ${UF2_FILE}
        COMMENT "Converting UF2 to CVBasic data (${BANK_SIZE}KB banks)"
        VERBATIM
    )
    
    target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_BASE}.h.bas)
    target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_BASE}.bas)
endfunction()

# Compile CVBasic source for a specific platform
function(visrealm_cvbasic_compile TARGET PLATFORM_FLAGS SOURCE_FILE ASM_OUTPUT LIB_PATH)
    if(NOT CVBASIC_EXECUTABLE)
        message(FATAL_ERROR "CVBasic not found - cannot compile ${SOURCE_FILE}")
    endif()
    
    set(CVBASIC_CMD ${CVBASIC_EXECUTABLE})
    
    # Add platform flags
    if(PLATFORM_FLAGS)
        list(APPEND CVBASIC_CMD ${PLATFORM_FLAGS})
    endif()
    
    # Add source, output, and library path
    list(APPEND CVBASIC_CMD ${SOURCE_FILE} ${ASM_OUTPUT} ${LIB_PATH})
    
    get_filename_component(SOURCE_DIR ${SOURCE_FILE} DIRECTORY)
    get_filename_component(ASM_DIR ${ASM_OUTPUT} DIRECTORY)
    
    add_custom_command(
        OUTPUT ${ASM_OUTPUT}
        COMMAND ${CVBASIC_CMD}
        DEPENDS ${SOURCE_FILE}
        WORKING_DIRECTORY ${SOURCE_DIR}
        COMMENT "Compiling CVBasic: ${SOURCE_FILE} -> ${ASM_OUTPUT}"
        VERBATIM
    )
    
    target_sources(${TARGET} PRIVATE ${ASM_OUTPUT})
endfunction()

# Assemble with GASM80 (for most platforms)
function(visrealm_gasm80_assemble TARGET ASM_FILE ROM_OUTPUT)
    if(NOT GASM80_EXECUTABLE)
        message(FATAL_ERROR "GASM80 not found - cannot assemble ${ASM_FILE}")
    endif()
    
    add_custom_command(
        OUTPUT ${ROM_OUTPUT}
        COMMAND ${GASM80_EXECUTABLE} ${ASM_FILE} -o ${ROM_OUTPUT}
        DEPENDS ${ASM_FILE}
        COMMENT "Assembling with GASM80: ${ASM_FILE} -> ${ROM_OUTPUT}"
        VERBATIM
    )
    
    target_sources(${TARGET} PRIVATE ${ROM_OUTPUT})
endfunction()

# Assemble with XAS99 (for TI-99 platform)
function(visrealm_xas99_assemble TARGET ASM_FILE BIN_OUTPUT CART_OUTPUT TITLE)
    if(NOT XAS99_SCRIPT)
        message(WARNING "XAS99 not found - skipping TI-99 assembly")
        return()
    endif()
    
    get_filename_component(ASM_DIR ${ASM_FILE} DIRECTORY)
    get_filename_component(ASM_NAME ${ASM_FILE} NAME_WE)
    set(BIN_FILE ${ASM_DIR}/${ASM_NAME}_b00.bin)
    
    add_custom_command(
        OUTPUT ${BIN_FILE}
        COMMAND ${PYTHON} ${XAS99_SCRIPT} -b -R ${ASM_FILE}
        DEPENDS ${ASM_FILE}
        WORKING_DIRECTORY ${ASM_DIR}
        COMMENT "Assembling with XAS99: ${ASM_FILE}"
        VERBATIM
    )
    
    # Link to TI cartridge format using configured linkticart script
    add_custom_command(
        OUTPUT ${CART_OUTPUT}
        COMMAND ${PYTHON} ${LINKTICART_SCRIPT} ${BIN_FILE} ${CART_OUTPUT} ${TITLE}
        DEPENDS ${BIN_FILE} ${LINKTICART_SCRIPT}
        COMMENT "Creating TI cartridge: ${CART_OUTPUT}"
        VERBATIM
    )
    
    target_sources(${TARGET} PRIVATE ${CART_OUTPUT})
endfunction()