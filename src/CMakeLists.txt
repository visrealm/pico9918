cmake_minimum_required(VERSION 3.13)

set(PROGRAM pico9918)

add_executable(${PROGRAM} )

target_sources(${PROGRAM} PRIVATE main.c gpu.c clocks.pio.h tms9918.pio.h Thumb9900.S)

pico_set_program_name(${PROGRAM} "pico9918")
pico_set_program_version(${PROGRAM} "0.4.2")

# compile-time options to ease development between JasonACT and I
set(PICO9918_NO_SPLASH 0)

# generate image array source files from png images
if (PICO9918_NO_SPLASH EQUAL 0)
visrealm_generate_image_source_ram(${PROGRAM} splash res/splash.png )
endif()

# generate header file from pio
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/clocks.pio)
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/tms9918.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROGRAM} 0)
pico_enable_stdio_usb(${PROGRAM} 0)

pico_set_binary_type(${PROGRAM} copy_to_ram)   # TOO SLOW TO BOOT

add_definitions(-DPICO9918_NO_SPLASH=${PICO9918_NO_SPLASH})
add_definitions(-DPICO_DISABLE_SHARED_IRQ_HANDLERS=1)
add_definitions(-DPICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
add_definitions(-DPICO_PANIC_FUNCTION=)

# Add the standard include files to the build
target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

# Add any user requested libraries
target_link_libraries(${PROGRAM} PUBLIC
        pico_stdlib
        pico_multicore
        hardware_dma        
        hardware_pio        
        pico9918-vga
        vrEmuTms9918)

pico_add_extra_outputs(${PROGRAM})

add_subdirectory(pio-utils)
add_subdirectory(vga)
