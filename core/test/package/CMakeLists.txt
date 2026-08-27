# Consumer smoke test: a separate project that finds the INSTALLED library.
#
#   cmake -S . -B ../../../build-install -DCMAKE_INSTALL_PREFIX=<staging>
#   cmake --build ../../../build-install --target install
#   cmake -S test/package -B <build> -DCMAKE_PREFIX_PATH=<staging>
#   cmake --build <build>
#
# This is what proves the exported package works. A `package` CI job without it
# proves only that cpack ran: an install tree can be missing a header, export an
# absolute source path, or lose a PUBLIC compile definition, and the archive is
# produced either way.
cmake_minimum_required(VERSION 3.22)
project(pico9918_core_consumer C)

set(CMAKE_C_STANDARD 99)

find_package(pico9918_core CONFIG REQUIRED)

add_executable(consumer consumer.c)
target_link_libraries(consumer PRIVATE pico9918::core)
