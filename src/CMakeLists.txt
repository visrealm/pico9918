set(PROGRAM pico9918)

#add_subdirectory(pio-utils)

add_executable(${PROGRAM})

target_sources(${PROGRAM} PRIVATE main.c tms9918.pio.h)

# generate header file from pio
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/tms9918.pio)

pico_add_extra_outputs(${PROGRAM})

target_link_libraries(${PROGRAM} PUBLIC
        pico_stdlib
        pico_multicore
        hardware_pio        
        pico9918-vga
        vrEmuTms9918
        vrEmuTms9918Util)

add_subdirectory(pio-utils)
add_subdirectory(tms9918)
