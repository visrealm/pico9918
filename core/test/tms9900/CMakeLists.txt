set(PROGRAM tms9900_test)

# On a board the assembly core is the subject and the portable one is the cross-check.
# Off it there is no assembly core to compare against, so the same cases become the
# portable core's own per-instruction test - the only one that runs without hardware.
if(DEFINED PICO_BUILD OR DEFINED PICO_PLATFORM OR DEFINED PICO_SDK_PATH)
    set(TMS9900_ON_DEVICE ON)
else()
    set(TMS9900_ON_DEVICE OFF)
endif()

if(TMS9900_ON_DEVICE)

# The board runs whichever core its platform selects, so the test links that one
# and names it - the same rule as src/CMakeLists.txt.
if(PICO_PLATFORM STREQUAL "rp2040")
    set(TMS9900_ASM_CORE "m0")
else()
    set(TMS9900_ASM_CORE "m33")
endif()

# Compile the two cores directly into the test - do not use the pico9918_core
# library, which may have been built with PICO9918_GPU_C_CORE=ON (excluding asm).
add_executable(${PROGRAM}
    tms9900_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu/tms9900.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu/platform/thumb9900_${TMS9900_ASM_CORE}.S
)

target_include_directories(${PROGRAM} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu
)

target_link_libraries(${PROGRAM} PUBLIC
    pico_stdlib
)

target_compile_definitions(${PROGRAM} PRIVATE
    PICO_STACK_SIZE=0x2000
    TMS9900_ASM_CORE="${TMS9900_ASM_CORE}"
)

# Run everything from RAM, matching real pico9918 firmware
pico_set_binary_type(${PROGRAM} copy_to_ram)

pico_enable_stdio_usb(${PROGRAM} 1)
pico_enable_stdio_uart(${PROGRAM} 1)

pico_add_extra_outputs(${PROGRAM})

else()

add_executable(${PROGRAM}
    tms9900_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu/tms9900.c
)

target_include_directories(${PROGRAM} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu
)

endif()
