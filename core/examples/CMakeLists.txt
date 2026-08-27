# The examples, built against the library however it is available.
#
# Standalone, against an installed package:
#
#   cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH=<staging>
#   cmake --build build-examples
#
# Or as part of the library's own build, where PICO9918_EXAMPLES adds it and the
# target is already defined. Either way an example links `pico9918::core` and
# includes only the public headers, which is what keeps it an example of using
# the library rather than of building it.
cmake_minimum_required(VERSION 3.22)
project(pico9918_core_examples C)

set(CMAKE_C_STANDARD 99)

if(NOT TARGET pico9918::core)
  find_package(pico9918_core CONFIG REQUIRED)
endif()

# Mirrors the library's own default, so an in-tree build adds whichever examples the
# library it just built can actually run. Standalone, pass it to match the package.
if(NOT DEFINED PICO9918_MODE)
  set(PICO9918_MODE 0)
endif()

add_executable(render_frame render_frame.c)
target_link_libraries(render_frame PRIVATE pico9918::core)

add_executable(host_bus host_bus.c)
target_link_libraries(host_bus PRIVATE pico9918::core)

if(PICO9918_MODE GREATER_EQUAL 1)
  add_executable(f18a_modes f18a_modes.c)
  target_link_libraries(f18a_modes PRIVATE pico9918::core)

  # The GPU example runs Tursi's Mandelbrot, which lives once in the test data and is
  # credited there. The path is passed in rather than the binary copied, so there is
  # still one copy of somebody else's program in this repository.
  add_executable(gpu_program gpu_program.c)
  target_link_libraries(gpu_program PRIVATE pico9918::core)
  target_compile_definitions(gpu_program PRIVATE
    MANDEL_BIN="${CMAKE_CURRENT_SOURCE_DIR}/../test/suite/data/gpu-programs/mandel.bin")
else()
  message(STATUS "pico9918 examples: f18a_modes and gpu_program need PICO9918_MODE=1")
endif()
