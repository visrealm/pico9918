set(PROGRAM tms9900_test)

# Compile the two cores directly into the test - do not use the pico9918_core
# library, which may have been built with PICO9918_GPU_C_CORE=ON (excluding asm).
add_executable(${PROGRAM}
    tms9900_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu/tms9900.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu/platform/thumb9900_m0.S
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
)

# Run everything from RAM, matching real pico9918 firmware
pico_set_binary_type(${PROGRAM} copy_to_ram)

pico_enable_stdio_usb(${PROGRAM} 1)
pico_enable_stdio_uart(${PROGRAM} 1)

pico_add_extra_outputs(${PROGRAM})
