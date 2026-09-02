# The post-palette pixel path: the LUT layout classes and the scanline geometry
# they are consumed with. Needs PICO9918_TEXT80_8BPP=ON - without the tier there
# is no wide row and the test reports itself as not applicable.
set(PROGRAM pixel_test)

add_executable(${PROGRAM} pixel_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

# Reaches into impl/ for the palette LUT, so it must see the same struct layout
# the library was compiled with - PICO9918_MODE gates it.
target_compile_definitions(${PROGRAM} PRIVATE PICO9918_MODE=${PICO9918_MODE})

# CMAKE_BINARY_DIR/src: the generated pico9918_build_config.h the public headers
# assert against. The dependency that makes it exist first comes from the link
# above; no add_dependencies is needed.
target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
