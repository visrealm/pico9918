set(PROGRAM pico9918qc)

add_executable(${PROGRAM})

target_sources(${PROGRAM} PRIVATE qc.c)

pico_add_extra_outputs(${PROGRAM})
pico_enable_stdio_usb(${PROGRAM} 1)

target_link_libraries(${PROGRAM} PUBLIC
        pico_stdlib
        pico_time)

