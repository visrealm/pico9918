# The debugger surface: the map pico9918_debug_region publishes and the span read over
# it. F18A-only, for the same reason the API itself is - a PICO9918_MODE=0 library has no
# map to walk.
set(PROGRAM debug_test)

add_executable(${PROGRAM} debug_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

# Reaches into impl/ to seed the instance behind the API's back, which is the only way to
# prove the read is not simply echoing whatever wrote the byte - so it must see the same
# struct layout the library was compiled with.
target_compile_definitions(${PROGRAM} PRIVATE PICO9918_MODE=${PICO9918_MODE})

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
