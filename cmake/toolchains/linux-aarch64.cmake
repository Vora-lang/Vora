# Toolchain for cross-compiling to 64-bit ARM Linux (aarch64 / arm64)
# Requires: gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Install on Ubuntu/Debian:
#   sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Usage:
#   cmake -S . -B build --toolchain cmake/toolchains/linux-aarch64.cmake
#   cmake --build build

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++   CACHE FILEPATH "C++ compiler")

# Search only the cross-compiler's sysroot; do not leak host libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
