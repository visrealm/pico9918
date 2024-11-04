cmake_minimum_required(VERSION 3.13)


# compile-time options

set(PICO9918_VERSION   "0.4.3")
set(PICO9918_PCB_MAJOR_VER 0)
set(PICO9918_PCB_MINOR_VER 4)

set(PICO9918_SCANLINES 1)

# SCART RGBs options
set(PICO9918_SCART_RGBS 0)  # 1 for RGBs, 0 for VGA
set(PICO9918_SCART_PAL  0)  # 1 for PAL 576i, 0 for NTSC 480i


set(PICO9918_NO_SPLASH 0)
set(PICO9918_DIAG 0)

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

string(REPLACE "." "-" PICO9918_VERSION_STR "${PICO9918_VERSION}")

set(PICO9918_BINARY_SUFFIX -pcb-${PICO_BOARD}-v${PICO9918_PCB_MAJOR_VER}-${PICO9918_PCB_MINOR_VER}-${PICO9918_OUTPUT_STR}-build-${PICO9918_VERSION_STR})

if (${PICO9918_SCANLINES})
  set(PICO9918_BINARY_SUFFIX ${PICO9918_BINARY_SUFFIX}-sl)
endif()

if (${PICO9918_DIAG})
  set(PICO9918_BINARY_SUFFIX ${PICO9918_BINARY_SUFFIX}-diag)
endif()

set(PROGRAM pico9918${PICO9918_BINARY_SUFFIX})

add_executable(${PROGRAM} )

target_sources(${PROGRAM} PRIVATE main.c clocks.pio.h tms9918.pio.h)

pico_set_program_name(${PROGRAM} "pico9918")
pico_set_program_version(${PROGRAM} ${PICO9918_VERSION})
pico_set_program_description(${PROGRAM} "PICO9918 VDP for PCB v${PICO9918_PCB_MAJOR_VER}.${PICO9918_PCB_MINOR_VER}")
pico_set_program_url(${PROGRAM} "https://github.com/visrealm/pico9918")

# generate image array source files from png images
if (PICO9918_NO_SPLASH EQUAL 0)
visrealm_generate_image_source_ram(${PROGRAM} splash res/splash.png )
endif()

if (PICO9918_DIAG)
visrealm_generate_image_source_ram(${PROGRAM} font res/font_subset.png )
endif()

# generate header file from pio
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/clocks.pio)
pico_generate_pio_header(${PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/tms9918.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROGRAM} 0)
pico_enable_stdio_usb(${PROGRAM} 0)

pico_set_binary_type(${PROGRAM} copy_to_ram)

add_definitions(-DPICO9918_NO_SPLASH=${PICO9918_NO_SPLASH})
add_definitions(-DPICO9918_VERSION="${PICO9918_VERSION}")
add_definitions(-DPICO9918_PCB_MAJOR_VER=${PICO9918_PCB_MAJOR_VER})
add_definitions(-DPICO9918_PCB_MINOR_VER=${PICO9918_PCB_MINOR_VER})
add_definitions(-DPICO9918_SCANLINES=${PICO9918_SCANLINES})
add_definitions(-DPICO9918_SCART_RGBS=${PICO9918_SCART_RGBS})
add_definitions(-DPICO9918_SCART_PAL=${PICO9918_SCART_PAL})
add_definitions(-DPICO9918_DIAG=${PICO9918_DIAG})

add_definitions(-DPICO_DISABLE_SHARED_IRQ_HANDLERS=1)
#add_definitions(-DPICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
add_definitions(-DPICO_PANIC_FUNCTION=)

target_link_libraries(${PROGRAM} PUBLIC        
        pico_stdlib
        pico_multicore
        hardware_dma
        hardware_pio
        hardware_adc
        pico9918-vga
        pico9918-gpu
        vrEmuTms9918)

pico_add_extra_outputs(${PROGRAM})

add_subdirectory(pio-utils)
add_subdirectory(vga)
add_subdirectory(gpu)