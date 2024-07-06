set(PROGRAM pico9918)

#add_subdirectory(pio-utils)

add_executable(${PROGRAM})

target_sources(${PROGRAM} PRIVATE main.c palette.c clocks.pio.h)

# generate image array source files from png images
visrealm_generate_image_source_ram(${PROGRAM} splash res/splash.png )

# generate header file from pio
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/clocks.pio)

pico_add_extra_outputs(${PROGRAM})
pico_enable_stdio_usb(${PROGRAM} 0)
pico_enable_stdio_uart(${PROGRAM} 0)

add_compile_options(-O3 -g3)

#pico_set_binary_type(${PROGRAM} copy_to_ram)   # TOO SLOW TO BOOT

target_link_libraries(${PROGRAM} PUBLIC
        pico_stdlib
        pico_multicore
        hardware_pio        
        pico9918-vga
        vrEmuTms9918)

add_subdirectory(pio-utils)
add_subdirectory(vga)
