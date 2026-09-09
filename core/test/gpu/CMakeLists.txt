# pico9918_gpu_set_clock: the library running an armed program itself, from the write
# that arms it.
set(PROGRAM gpu_test)

add_executable(${PROGRAM} gpu_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
