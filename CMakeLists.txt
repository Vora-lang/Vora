cmake_minimum_required(VERSION 3.16)

project(Vora VERSION 0.17.0 DESCRIPTION "Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tell MSVC to treat source files as UTF-8 (eliminates C4819 code page warnings
# on systems where the default locale uses a narrow code page like 936/GBK).
# /utf-8 sets both source and execution character sets to UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ──────────────────────────────────────────────────
# OBJECT library — all source files except main.cpp
# Used by both Vora.exe and Vora_tests.exe (zero
# duplicate compilation).
# ──────────────────────────────────────────────────

add_library(vora_lib OBJECT

    src/ast/ast_printer.cpp
    src/ast/program.cpp
    src/ast/expr.cpp
    src/ast/stmt.cpp

    src/formatter/formatter.cpp

    src/gc/gc_heap.cpp

    src/lexer/token.cpp
    src/lexer/lexer.cpp

    src/parser/parser.cpp

    src/runtime/builtins.cpp
    src/runtime/value.cpp
    src/runtime/native_function.cpp
    src/runtime/vora_function.cpp
    src/runtime/environment.cpp
    src/runtime/runtime_error.cpp

    src/vm/chunk.cpp
    src/vm/compiler.cpp
    src/vm/compiler_expr.cpp
    src/vm/compiler_stmt.cpp
    src/vm/value_ops.cpp
    src/vm/vm.cpp
)

target_include_directories(vora_lib PRIVATE src)

target_compile_features(vora_lib PRIVATE cxx_std_17)

# ──────────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────────

add_executable(Vora
    src/main.cpp
    $<TARGET_OBJECTS:vora_lib>

    # Windows resource file (icon + version info) — ignored on non-MSVC
    res/vora.rc
)

target_include_directories(
    Vora PRIVATE src
)

target_compile_features(
    Vora PRIVATE cxx_std_17
)

# ──────────────────────────────────────────────────
# Install
# ──────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS Vora
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ──────────────────────────────────────────────────
# CTest — one test per .va file
# ──────────────────────────────────────────────────

enable_testing()

file(GLOB TEST_LEXER        tests/lexer/*.va)
file(GLOB TEST_PARSER       tests/parser/*.va)
file(GLOB TEST_RUNTIME      tests/runtime/*.va)
file(GLOB TEST_INTERPRETER  tests/interpreter/*.va)
file(GLOB TEST_FORMATTER    tests/formatter/*.va)

# file(GLOB) may return absolute or relative paths depending on generator.
# Use WORKING_DIRECTORY so relative paths resolve correctly, and strip the
# source-dir prefix if the path is already absolute.

# VM mode (default) tests
foreach(test_file ${TEST_LEXER} ${TEST_PARSER} ${TEST_RUNTIME} ${TEST_INTERPRETER} ${TEST_FORMATTER})
    get_filename_component(test_name ${test_file} NAME_WLE)
    # Strip CMAKE_CURRENT_SOURCE_DIR prefix if present (handles abs paths)
    set(rel_path "${test_file}")
    if(rel_path MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}")
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" rel_path "${rel_path}")
    endif()
    add_test(
        NAME "vm-${test_name}"
        COMMAND Vora "${rel_path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endforeach()

# ──────────────────────────────────────────────────
# C++ unit tests (doctest)
# ──────────────────────────────────────────────────

# Download doctest at configure time if not vendored
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/doctest.h")
    message(STATUS "Downloading doctest v2.4.11 …")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/doctest.h"
        SHOW_PROGRESS
    )
endif()

add_executable(Vora_tests
    $<TARGET_OBJECTS:vora_lib>
    tests/unit/test_main.cpp
    tests/unit/test_value.cpp
    tests/unit/test_chunk.cpp
    tests/unit/test_lexer.cpp
    tests/unit/test_parser.cpp
    tests/unit/test_compiler.cpp
    tests/unit/test_vm.cpp
    tests/unit/test_formatter.cpp
)

target_include_directories(Vora_tests PRIVATE
    src
    tests/unit
)

target_compile_features(Vora_tests PRIVATE cxx_std_17)

# Define DOCTEST_CONFIG_IMPLEMENT only for the test driver file.
set_source_files_properties(
    tests/unit/test_main.cpp
    PROPERTIES COMPILE_DEFINITIONS "DOCTEST_CONFIG_IMPLEMENT"
)

# Register one CTest per test file via doctest --test-case filter.
# Each test file's cases are prefixed with the module name
# (e.g. "value_", "chunk_", "lexer_"), so -tc="prefix*" isolates
# failures to the module level.
function(vora_unit_test test_file case_prefix)
    get_filename_component(file_name ${test_file} NAME_WE)
    add_test(
        NAME "unit-${file_name}"
        COMMAND Vora_tests -tc="${case_prefix}*"
    )
endfunction()

vora_unit_test(tests/unit/test_value.cpp    "value")
vora_unit_test(tests/unit/test_chunk.cpp    "chunk")
vora_unit_test(tests/unit/test_lexer.cpp    "lexer")
vora_unit_test(tests/unit/test_parser.cpp   "parser")
vora_unit_test(tests/unit/test_compiler.cpp "compiler")
vora_unit_test(tests/unit/test_vm.cpp       "vm")
vora_unit_test(tests/unit/test_formatter.cpp "fmt")


# ═══════════════════════════════════════════════════════════════════════════
# Fuzzer target (libFuzzer) — Linux/clang only
# ═══════════════════════════════════════════════════════════════════════════

option(VORA_FUZZER "Build libFuzzer harness for VM execution layer" OFF)

if(VORA_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "VORA_FUZZER requires Clang (libFuzzer is clang-only)")
    endif()

    add_executable(Vora_fuzzer
        $<TARGET_OBJECTS:vora_lib>
        tests/fuzz/fuzz_vm.cpp
    )

    target_include_directories(Vora_fuzzer PRIVATE src)

    target_compile_features(Vora_fuzzer PRIVATE cxx_std_17)

    target_compile_options(Vora_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )

    target_link_options(Vora_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined
    )

    # Seed corpus directory
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus")

    message(STATUS "Fuzzer target enabled (Vora_fuzzer)")
endif()

# ═══════════════════════════════════════════════════════════════════════════
# Install rules
# ═══════════════════════════════════════════════════════════════════════════

include(GNUInstallDirs)
install(TARGETS Vora
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Icon installation ───────────────────────────────────────────────────
if(WIN32)
    # ICO is embedded in the .exe via vora.rc — nothing extra to install.
    # But copy the ICO alongside for CPack/NSIS reference.
    install(FILES res/vora.ico DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(APPLE)
    install(DIRECTORY res/vora.iconset DESTINATION share/vora/icons)
else()
    # Linux: SVG → scalable icon, largest PNG → fixed-size fallback
    install(FILES vora_logo.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME vora.svg
    )
    install(FILES res/vora.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps
        RENAME vora.png
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════
# CPack — Package generation (.deb / .rpm / .pkg.tar.xz / .msi)
# ═══════════════════════════════════════════════════════════════════════════

set(CPACK_PACKAGE_NAME "vora")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Vora-lang")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation."
)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Vora-lang/Vora")
# WiX (Windows) and productbuild (macOS) require a license file with a
# recognised extension (.rtf, .html, .txt). The repo LICENSE has no
# extension, so use the RTF copy from res/ if available.
if(EXISTS "${CMAKE_SOURCE_DIR}/res/LICENSE.rtf")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/res/LICENSE.rtf")
else()
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

# Architecture-aware package file name
#   e.g. vora-0.9.0-Linux-x86_64.deb  /  vora-0.9.0-Linux-aarch64.rpm
# Use CMAKE_SYSTEM_PROCESSOR (target architecture) so cross-compiled
# packages are named correctly (not the host runner's arch).
set(CPACK_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CPACK_TARGET_ARCH}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")

# ── Cross-compilation RPM strip fix ──────────────────────────────────────────
# When cross-compiling (host != target), the host's strip(1) cannot process
# target binaries. RPM's brp-strip still runs regardless of CPACK_STRIP_FILES,
# so we must override the RPM __strip macro to a no-op.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    message(STATUS "Cross-compile detected (host=${CMAKE_HOST_SYSTEM_PROCESSOR}, target=${CMAKE_SYSTEM_PROCESSOR}) — RPM stripping disabled")
    set(CPACK_RPM_SPEC_MORE_DEFINE "%define __strip /bin/true")
endif()

# ── Debian / Ubuntu (.deb) ────────────────────────────────────────────────
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vora-lang")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

# Map CMake system processor → Debian architecture
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "i386" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i686")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ── Red Hat / Fedora (.rpm) ───────────────────────────────────────────────
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")

# Map CMake system processor → RPM architecture
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "armv7hl")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "i386" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i686")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "i686")
else()
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ── Generator selection ───────────────────────────────────────────────────

if(WIN32)
    # Windows: prefer WIX (.msi) if available, fall back to NSIS + ZIP
    set(CPACK_GENERATOR "ZIP")
    find_program(WIX_CANDLE_EXECUTABLE candle)
    find_program(WIX_LIGHT_EXECUTABLE light)
    if(WIX_CANDLE_EXECUTABLE AND WIX_LIGHT_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "WIX")
        set(CPACK_WIX_UPGRADE_GUID "A9A1F393-D8B7-4CD2-9DC6-E2A8DE7EB7A2")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/res/vora.ico")
    else()
        message(STATUS "WiX Toolset not found — .msi disabled; will build .zip only")
        message(STATUS "  Install WiX: dotnet tool install -g wix")
    endif()
    find_program(NSIS_EXECUTABLE makensis)
    if(NSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
    endif()
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
    # productbuild for .pkg (requires Xcode)
    find_program(PBUILD_EXECUTABLE pkgbuild)
    find_program(PRDIST_EXECUTABLE productbuild)
    if(PBUILD_EXECUTABLE AND PRDIST_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "productbuild")
    endif()
else()
    # Linux: all three package formats
    set(CPACK_GENERATOR "TGZ;TXZ")

    # .deb — requires dpkg
    find_program(DPKG_EXECUTABLE dpkg)
    if(DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
    else()
        message(STATUS "dpkg not found — .deb disabled")
    endif()

    # .rpm — requires rpmbuild
    find_program(RPMBUILD_EXECUTABLE rpmbuild)
    if(RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
    else()
        message(STATUS "rpmbuild not found — .rpm disabled")
    endif()
endif()

message(STATUS "CPack generators: ${CPACK_GENERATOR}")

include(CPack)
