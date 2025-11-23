cmake_minimum_required(VERSION 3.13)


# compile-time options

# Version numbers and build options now defined in root CMakeLists.txt

# end compile-time options


# set up variables for the build process

execute_process(
    COMMAND git symbolic-ref --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PICO9918_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (${PICO9918_SCART_RGBS})
  if (${PICO9918_SCART_PAL})
    set(PICO9918_OUTPUT_STR "rgbs-pal576i")
  else()
    set(PICO9918_OUTPUT_STR "rgbs-ntsc480i")
  endif()
else()
  set(PICO9918_OUTPUT_STR "vga")
endif()

set(PICO9918_VERSION  "${PICO9918_MAJOR_VER}.${PICO9918_MINOR_VER}.${PICO9918_PATCH_VER}")
string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

if (NOT ${PICO_BOARD} STREQUAL "pico9918")
  set(PICO_BOARD_STR -${PICO_BOARD})
endif()

set(PICO9918_BINARY_SUFFIX ${PICO_BOARD_STR}-${PICO9918_OUTPUT_STR}-build-v${PICO9918_VERSION_STR})

if (${PICO9918_DIAG})
  set(PICO9918_BINARY_SUFFIX ${PICO9918_BINARY_SUFFIX}-diag)
endif()

set(PROGRAM pico9918${PICO9918_BINARY_SUFFIX})

add_executable(${PROGRAM} )

target_sources(${PROGRAM} PRIVATE main.c config.c diag.c flash.c gpio.c splash.c temperature.c palette.c palette.h clocks.pio.h tms9918.pio.h palconv.pio.h)

pico_set_program_name(${PROGRAM} "pico9918")
pico_set_program_version(${PROGRAM} ${PICO9918_VERSION})
pico_set_program_description(${PROGRAM} "PICO9918 VDP")
pico_set_program_url(${PROGRAM} "https://github.com/visrealm/pico9918")

# generate image array source files from png images
if (NOT PICO9918_NO_SPLASH)
visrealm_generate_image_source_ram(${PROGRAM} bmp_splash res/splash.png )
endif()

visrealm_generate_image_source_ram(${PROGRAM} bmp_font res/font.png )

# generate header file from pio
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/clocks.pio)
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/palconv.pio)
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/tms9918.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROGRAM} 0)
pico_enable_stdio_usb(${PROGRAM} 0)

pico_set_binary_type(${PROGRAM} copy_to_ram)

target_compile_definitions(${PROGRAM} PRIVATE
    PICO9918_NO_SPLASH=$<BOOL:${PICO9918_NO_SPLASH}>
    PICO9918_SCART_RGBS=$<BOOL:${PICO9918_SCART_RGBS}>
    PICO9918_SCART_PAL=$<BOOL:${PICO9918_SCART_PAL}>
    PICO9918_DIAG=$<BOOL:${PICO9918_DIAG}>
    PICO9918_VERSION="${PICO9918_VERSION}"
    PICO9918_MAJOR_VER=${PICO9918_MAJOR_VER}
    PICO9918_MINOR_VER=${PICO9918_MINOR_VER}
    PICO9918_PATCH_VER=${PICO9918_PATCH_VER}
    PICO_DISABLE_SHARED_IRQ_HANDLERS=1
    PICO_PANIC_FUNCTION=
)


target_link_libraries(${PROGRAM} PUBLIC        
        pico_stdlib
        pico_multicore
        hardware_dma
        hardware_pio
        hardware_adc
        hardware_flash
        pico9918-vga
        pico9918-gpu
        vrEmuTms9918)

# Set the executable suffix to .elf for all platforms
set_target_properties(${PROGRAM} PROPERTIES SUFFIX .elf)

pico_add_extra_outputs(${PROGRAM})

# Copy final artifacts to dist directory for unified output
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dist/${PROGRAM}.uf2
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:${PROGRAM}>/${PROGRAM}.uf2 ${CMAKE_BINARY_DIR}/dist/
    DEPENDS ${PROGRAM}
    COMMENT "Copying ${PROGRAM}.uf2 to dist/"
)

# Create a target for the dist uf2 file
add_custom_target(${PROGRAM}_dist DEPENDS ${CMAKE_BINARY_DIR}/dist/${PROGRAM}.uf2)

# Create a generic firmware target for easier building
add_custom_target(firmware DEPENDS ${CMAKE_BINARY_DIR}/dist/${PROGRAM}.uf2)

add_subdirectory(pio-utils)
add_subdirectory(vga)
add_subdirectory(gpu)