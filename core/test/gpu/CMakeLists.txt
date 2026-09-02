# pico9918_gpu_set_clock: the library running an armed program itself, from the write
# that arms it. F18A-only - a PICO9918_MODE=0 library has no GPU to link against.
set(PROGRAM gpu_test)

add_executable(${PROGRAM} gpu_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

# Reaches into impl/ for the instance's VRAM, so it must see the same struct layout the
# library was compiled with - PICO9918_MODE gates it.
target_compile_definitions(${PROGRAM} PRIVATE PICO9918_MODE=${PICO9918_MODE})

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
