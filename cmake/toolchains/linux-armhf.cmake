# Toolchain for cross-compiling to 32-bit ARM hard-float Linux (armhf / armv7)
# Requires: gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#
# Install on Ubuntu/Debian:
#   sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#
# Usage:
#   cmake -S . -B build --toolchain cmake/toolchains/linux-armhf.cmake
#   cmake --build build

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++   CACHE FILEPATH "C++ compiler")

# Search only the cross-compiler's sysroot; do not leak host libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
