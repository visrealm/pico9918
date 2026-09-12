# The config block's portable half: the identity check, the reset to defaults and the
# per-version migration. Independent of the renderer, so it runs at either line width.
set(PROGRAM config_test)

add_executable(${PROGRAM} config_test.c)
target_link_libraries(${PROGRAM} PRIVATE pico9918_core)

# CMAKE_BINARY_DIR/src: the generated pico9918_build_config.h the public headers
# assert against. The dependency that makes it exist first comes from the link
# above; no add_dependencies is needed.
target_include_directories(${PROGRAM} PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src)
