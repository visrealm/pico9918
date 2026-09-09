# Consumer smoke test: a separate project that finds the INSTALLED library.
#
#   cmake -S . -B ../../../build-install -DCMAKE_INSTALL_PREFIX=<staging>
#   cmake --build ../../../build-install --target install
#   cmake -S test/package -B <build> -DCMAKE_PREFIX_PATH=<staging>
#   cmake --build <build>
#
# This is what proves the exported static and shared packages work. A `package` CI job without it
# proves only that cpack ran: an install tree can be missing a header, export an
# absolute source path, or lose a PUBLIC compile definition, and the archive is
# produced either way.
cmake_minimum_required(VERSION 3.22)
project(pico9918_core_consumer C CXX)

# Deliberately older than the library's own C11: the package has to raise a consumer that
# asks for less, which is what target_compile_features(pico9918_core PUBLIC c_std_11) is
# for. Raise this and the propagation stops being tested.
set(CMAKE_C_STANDARD 99)

# The C++ side needs C++11 for static_assert, which is what a public header's compile-time
# assertion becomes when a C++ host includes it.
set(CMAKE_CXX_STANDARD 11)

find_package(pico9918_core CONFIG REQUIRED)

# The package job configures this project once for each linkage. Checking the imported
# target here catches a shared build exported as static (or one that leaks
# PICO9918_STATIC to its consumers) before the executable happens to link anyway on a
# permissive platform.
option(PICO9918_EXPECT_SHARED "Expect an installed shared pico9918-core target" OFF)
get_target_property(PICO9918_IMPORTED_TYPE pico9918::core TYPE)
get_target_property(PICO9918_IMPORTED_DEFS pico9918::core INTERFACE_COMPILE_DEFINITIONS)
list(FIND PICO9918_IMPORTED_DEFS "PICO9918_STATIC" PICO9918_STATIC_DEF_INDEX)

if(PICO9918_EXPECT_SHARED)
  if(NOT PICO9918_IMPORTED_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "expected a shared pico9918::core, got ${PICO9918_IMPORTED_TYPE}")
  endif()
  if(NOT PICO9918_STATIC_DEF_INDEX EQUAL -1)
    message(FATAL_ERROR "shared pico9918::core exports PICO9918_STATIC to consumers")
  endif()
else()
  if(NOT PICO9918_IMPORTED_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "expected a static pico9918::core, got ${PICO9918_IMPORTED_TYPE}")
  endif()
  if(PICO9918_STATIC_DEF_INDEX EQUAL -1)
    message(FATAL_ERROR "static pico9918::core does not export PICO9918_STATIC")
  endif()
endif()

add_executable(consumer consumer.c)
target_link_libraries(consumer PRIVATE pico9918::core)

# A C++ consumer, because C cannot see either failure it covers: a C11 construct in a
# public header, and a declaration whose name mangles and then does not link.
add_executable(consumer_cpp consumer.cpp)
target_link_libraries(consumer_cpp PRIVATE pico9918::core)

# bare.c is deliberately not a target here. It exists to be compiled with no build
# system, which is the only way to test that the generated header rather than this
# imported target is what tells a consumer the instance ABI. ci.sh package runs it.
