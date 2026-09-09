# The debugger surface: the map pico9918_debug_region publishes and the span read over it.
set(PROGRAM debug_test)

add_executable(${PROGRAM} debug_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
