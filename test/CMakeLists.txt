set(PROGRAM pico9918test)

add_executable(${PROGRAM})

# generate image array source files from png images
visrealm_generate_bindata_source(${PROGRAM} breakout res/BREAKOUT.* )


target_sources(${PROGRAM} PRIVATE test.c font.c)

pico_add_extra_outputs(${PROGRAM})

target_link_libraries(${PROGRAM} PUBLIC
        pico_stdlib)

