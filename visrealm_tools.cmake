cmake_minimum_required(VERSION 3.12)


set(IMG_CONV ${CMAKE_SOURCE_DIR}/tools/img2carray.py)
set(BIN_CONV ${CMAKE_SOURCE_DIR}/tools/bin2carray.py)
set(PYTHON python3)

# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_image_source TARGET DST ROMSRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${ROMSRC})
  cmake_path(GET fullSrc PARENT_PATH srcPath)

  set (RAMSRCARG)
  set (extra_args ${ARGN})
  list(LENGTH extra_args extra_count)
  if (${extra_count} GREATER 0)
    list(GET extra_args 0 RAMSRC)
    set(RAMSRCARG -r ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC})
  endif()
  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${IMG_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${ROMSRC} ${RAMSRCARG} -o ${DST}.c
      DEPENDS ${IMG_CONV} ${srcPath}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()

# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_image_source_ram TARGET DST RAMSRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC})
  cmake_path(GET fullSrc PARENT_PATH srcPath)

  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${IMG_CONV} -r ${CMAKE_CURRENT_SOURCE_DIR}/${RAMSRC} -o ${DST}.c
      DEPENDS ${IMG_CONV} ${srcPath}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()




# custom function to generate source code from images using tools/img2carray.py
function(visrealm_generate_bindata_source TARGET DST SRC)
  set(fullSrc ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
  cmake_path(GET fullSrc PARENT_PATH srcPath)

  add_custom_command(
      OUTPUT ${DST}.c ${DST}.h
      COMMAND ${PYTHON} ${BIN_CONV} -i ${CMAKE_CURRENT_SOURCE_DIR}/${SRC} -o ${DST}.c
      DEPENDS ${BIN_CONV} ${srcPath}
  )
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${DST}.c)
endfunction()


# CVBasic build functions for PICO9918 Configurator

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
    
    # Link to TI cartridge format
    set(LINKTICART ${CMAKE_SOURCE_DIR}/configtool/tools/cvbasic/linkticart.py)
    add_custom_command(
        OUTPUT ${CART_OUTPUT}
        COMMAND ${PYTHON} ${LINKTICART} ${BIN_FILE} ${CART_OUTPUT} ${TITLE}
        DEPENDS ${BIN_FILE} ${LINKTICART}
        COMMENT "Creating TI cartridge: ${CART_OUTPUT}"
        VERBATIM
    )
    
    target_sources(${TARGET} PRIVATE ${CART_OUTPUT})
endfunction()
