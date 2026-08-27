# The Python module, built against the library however it is available.
#
# Standalone, against an installed package:
#
#   cmake -S bindings/python -B build-py -DCMAKE_PREFIX_PATH=<staging>
#   cmake --build build-py
#
# Or as part of the library's own build, where PICO9918_PYTHON_BINDING adds it. Either
# way the archive has to be position-independent, which is what the root
# CMakeLists sets and what -DCMAKE_POSITION_INDEPENDENT_CODE=ON gives an install.
#
# The extension is written against the CPython C API directly, so this directory has
# no dependency the library does not, and nothing here needs a C++ toolchain.
cmake_minimum_required(VERSION 3.22)
project(pico9918_core_bindings_python C)

# Set here rather than inherited: the root CMakeLists sets it as a normal variable,
# which reaches this file as a subdirectory but not as the standalone build above.
set(CMAKE_C_STANDARD 11)

if(NOT TARGET pico9918::core)
  find_package(pico9918_core CONFIG REQUIRED)
endif()

# Development.Module for Python3_add_library, which will not run without the target it
# defines. Interpreter for WITH_SOABI, which asks the interpreter for its extension
# suffix - without it the module is a bare pico9918.so built for whichever Python the
# headers came from.
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

Python3_add_library(pico9918_py MODULE WITH_SOABI module.c)
set_target_properties(pico9918_py PROPERTIES OUTPUT_NAME pico9918)
target_link_libraries(pico9918_py PRIVATE pico9918::core)

if(MINGW)
  # An extension module is loaded by an interpreter that knows nothing about mingw's
  # runtime, and Python 3.8 stopped searching PATH for an extension's dependencies -
  # so a .pyd that needs libgcc_s_seh-1.dll beside it does not import at all.
  target_link_options(pico9918_py PRIVATE -static-libgcc -static)
endif()
