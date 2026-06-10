# Toolchain for building macOS universal binary (x86_64 + arm64)
#
# This builds a single "fat" binary that runs natively on both
# Intel Macs (x86_64) and Apple Silicon Macs (arm64).
#
# Usage:
#   cmake -S . -B build --toolchain cmake/toolchains/macos-universal.cmake
#   cmake --build build

set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build universal binary for Intel + Apple Silicon")

# Ensure the SDK supports both architectures
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (arm64 requires 11.0+)")

message(STATUS "macos-universal: CMAKE_OSX_ARCHITECTURES = ${CMAKE_OSX_ARCHITECTURES}")
