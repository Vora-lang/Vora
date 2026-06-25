cmake_minimum_required(VERSION 3.16)

project(Vora VERSION 0.24.0 DESCRIPTION "Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.")

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

add_library(vora_lib STATIC

    src/common/error_reporter.cpp

    src/json_rpc/json_rpc.cpp
    src/json_rpc/transport.cpp
    src/json_rpc/message_router.cpp

    src/ast/ast_printer.cpp
    src/ast/param_decl.cpp
    src/ast/binding_pattern.cpp
    src/ast/program.cpp
    src/ast/expr.cpp
    src/ast/stmt.cpp

    src/lsp/semantic_analyzer.cpp

    src/formatter/formatter.cpp

    src/gc/gc_heap.cpp

    src/lexer/token.cpp
    src/lexer/lexer.cpp

    src/parser/parser.cpp

    src/runtime/bound_method.cpp
    src/runtime/builtins.cpp
    src/runtime/class_constructor.cpp
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

target_include_directories(vora_lib PRIVATE src include)

target_compile_features(vora_lib PRIVATE cxx_std_17)

# ──────────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────────

# Generate Windows resource file from template (version from project())
configure_file(
    "${CMAKE_SOURCE_DIR}/res/vora.rc.in"
    "${CMAKE_BINARY_DIR}/vora.rc"
    @ONLY
)

# Generate WiX PATH environment fragment (version from project())
configure_file(
    "${CMAKE_SOURCE_DIR}/res/path_env.wxs.in"
    "${CMAKE_BINARY_DIR}/path_env.wxs"
    @ONLY
)

# Generate full WiX source for MSI (version + paths from project())
configure_file(
    "${CMAKE_SOURCE_DIR}/res/vora.wxs.in"
    "${CMAKE_BINARY_DIR}/vora.wxs"
    @ONLY
)

add_executable(Vora
    src/main.cpp

    # Windows resource file (icon + version info) — ignored on non-MSVC
    "${CMAKE_BINARY_DIR}/vora.rc"
)

target_link_libraries(Vora PRIVATE vora_lib)

target_include_directories(
    Vora PRIVATE src
)

target_compile_features(
    Vora PRIVATE cxx_std_17
)

target_compile_definitions(
    Vora PRIVATE VORA_VERSION="${PROJECT_VERSION}"
)

# ──────────────────────────────────────────────────
# Install — 导出可执行文件、静态库、头文件、std/ 库
# ──────────────────────────────────────────────────

include(GNUInstallDirs)

# 可执行文件
install(TARGETS Vora
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 静态库 + 头文件（供 C++ 嵌入使用）
install(TARGETS vora_lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# ── 生成单文件 vora.hpp（合并所有头文件） ────────────────────────────────
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    set(VORA_HPP "${CMAKE_BINARY_DIR}/vora.hpp")
    add_custom_command(
        OUTPUT "${VORA_HPP}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/amalgamate_headers.py"
                -o "${VORA_HPP}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/scripts/amalgamate_headers.py"
            "${CMAKE_SOURCE_DIR}/src/vora.h"
        COMMENT "Generating single-header vora.hpp..."
    )
    add_custom_target(vora_hpp ALL DEPENDS "${VORA_HPP}")
	    set(_msi_extra_deps vora_hpp)

    # 将 vora.hpp 安装到 SDK
    install(FILES "${VORA_HPP}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vora"
    )
endif()

# 公开头文件 — 保留 src/ 内的目录结构，使 `#include "vora.h"`
# 及所有内部 `#include "vm/vm.h"` 等路径都能正确解析。
install(FILES src/vora.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vora"
)

# src/ 下所有嵌入需要的头文件（排除 LSP / JSON-RPC 内部实现）
install(DIRECTORY src/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vora"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "lsp"        EXCLUDE
    PATTERN "json_rpc"   EXCLUDE
)

# 第三方依赖（nlohmann/json.hpp）
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vora"
    FILES_MATCHING
    PATTERN "*.hpp"
)

# 标准库模块（.va 文件）
install(DIRECTORY std/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vora/std"
    FILES_MATCHING
    PATTERN "*.va"
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

    # test_input.va requires stdin piping — handled separately below.
    if(test_name STREQUAL "test_input")
        continue()
    endif()

    # Strip CMAKE_CURRENT_SOURCE_DIR prefix if present (handles abs paths)
    set(rel_path "${test_file}")
    if(rel_path MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}")
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" rel_path "${rel_path}")
    endif()

    # Include parent directory in test name to avoid conflicts when test files
    # have the same basename in different subdirectories (e.g. interpreter/ and
    # runtime/ both have test_defer.va, test_match.va, test_set_map.va).
    get_filename_component(test_dir "${rel_path}" DIRECTORY)
    get_filename_component(test_dir_name "${test_dir}" NAME)
    add_test(
        NAME "vm-${test_dir_name}-${test_name}"
        COMMAND Vora "${rel_path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endforeach()

# ── test_input.va — requires piped stdin ────────────────────────────────
# The test expects three lines: "hello", "", "42" (the empty line tests that
# input() returns "" for blank lines; the last line has no trailing newline).
# We write the input to a file at configure time, then use a shell to
# redirect stdin (add_test COMMAND passes < literally, so a shell is needed).
#
# Bracket syntax produces literal LF newlines — std::getline handles them
# on all platforms.  The \r-strip in builtins.cpp covers terminal CRLF.
set(TEST_INPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/test_input_data.txt")
file(WRITE "${TEST_INPUT_FILE}" [=[
hello

42
]=])

if(WIN32)
    # file(GENERATE ...) evaluates $<TARGET_FILE:Vora> at generate time.
    # Use a multi-line string so the batch file ends with a real newline
    # (CMake does not interpret \n escape sequences).
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_test_input$<CONFIG>.bat"
        CONTENT "@$<TARGET_FILE:Vora> \"${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/test_input.va\" < \"${TEST_INPUT_FILE}\"
")
    add_test(
        NAME "vm-test_input"
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/run_test_input$<CONFIG>.bat"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
else()
    add_test(
        NAME "vm-test_input"
        COMMAND sh -c "\"$<TARGET_FILE:Vora>\" \"${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/test_input.va\" < \"${TEST_INPUT_FILE}\""
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

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
    tests/unit/test_main.cpp
    tests/unit/test_value.cpp
    tests/unit/test_chunk.cpp
    tests/unit/test_lexer.cpp
    tests/unit/test_parser.cpp
    tests/unit/test_compiler.cpp
    tests/unit/test_vm.cpp
    tests/unit/test_formatter.cpp
    tests/unit/test_semantic_analyzer.cpp
    tests/unit/test_json_rpc.cpp
)

target_include_directories(Vora_tests PRIVATE
    src
    include
    tests/unit
)

target_compile_features(Vora_tests PRIVATE cxx_std_17)

target_link_libraries(Vora_tests PRIVATE vora_lib)

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
vora_unit_test(tests/unit/test_semantic_analyzer.cpp "semantic")
vora_unit_test(tests/unit/test_json_rpc.cpp "jsonrpc|lsp")


# ═══════════════════════════════════════════════════════════════════════════
# Fuzzer target (libFuzzer) — Linux/clang only
# ═══════════════════════════════════════════════════════════════════════════

option(VORA_EMBED_DEMO "Build the C++ embedding example" OFF)
if(VORA_EMBED_DEMO)
    add_subdirectory(examples/embed)
endif()

option(VORA_FUZZER "Build libFuzzer harness for VM execution layer" OFF)

if(VORA_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "VORA_FUZZER requires Clang (libFuzzer is clang-only)")
    endif()

    # Instrument all vora_lib objects with fuzzer coverage (no-link variant —
    # libFuzzer's main() is provided only at the final Vora_fuzzer link step).
    # This avoids "multiple definition of main" during CMake's compiler detection
    # that would occur if -fsanitize=fuzzer were passed globally via CMAKE_FLAGS.
    target_compile_options(vora_lib PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
        -g
    )

    add_executable(Vora_fuzzer
        tests/fuzz/fuzz_vm.cpp
    )

    target_link_libraries(Vora_fuzzer PRIVATE vora_lib)

    target_include_directories(Vora_fuzzer PRIVATE src)

    target_compile_features(Vora_fuzzer PRIVATE cxx_std_17)

    target_compile_options(Vora_fuzzer PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
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
    # Lowercase launcher — lets users type "vora" instead of "Vora"
    install(FILES res/vora.cmd DESTINATION ${CMAKE_INSTALL_BINDIR})
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
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Vora")  # not "vora 0.21.0"
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
    # WiX v3/v4 (candle + light) — also check extracted v3 binaries
    find_program(WIX_CANDLE_EXECUTABLE candle
        HINTS "${WIX_ROOT}" "${WIX_ROOT}/bin"
              "C:/WiXv3"
              "C:/Program Files (x86)/WiX Toolset v3.14/bin")
    find_program(WIX_LIGHT_EXECUTABLE light
        HINTS "${WIX_ROOT}" "${WIX_ROOT}/bin"
              "C:/WiXv3"
              "C:/Program Files (x86)/WiX Toolset v3.14/bin")
    if(WIX_CANDLE_EXECUTABLE AND WIX_LIGHT_EXECUTABLE)
        # CPack WiX is available but we use a custom WXS for full control
        # (PATH env, registry, proper install dir). Keep WIX in CPack
        # for the auto-generated MSI as a fallback.
        list(APPEND CPACK_GENERATOR "WIX")
        set(CPACK_WIX_UPGRADE_GUID "A9A1F393-D8B7-4CD2-9DC6-E2A8DE7EB7A2")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/res/vora.ico")
        set(CPACK_WIX_CANDLE_EXECUTABLE "${WIX_CANDLE_EXECUTABLE}")
        set(CPACK_WIX_LIGHT_EXECUTABLE "${WIX_LIGHT_EXECUTABLE}")
        set(CPACK_WIX_CANDLE_EXTRA_FLAGS "-arch x64")
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

# ── Custom MSI target (full-featured: WixUI + PATH env + registry + bin/std) ─
# Uses WiX v3 candle.exe + light.exe (from C:/WiXv3) to build from the
# configured res/vora.wxs.in template. WiX v3 is used because WiX v7's
# WixUI extension has breaking changes that make UIRef unusable.
if(WIN32)
    find_program(WIX_CANDLE candle HINTS "C:/WiXv3")
    find_program(WIX_LIGHT light HINTS "C:/WiXv3")
    if(WIX_CANDLE AND WIX_LIGHT)
        # Determine the WiX tools directory for -ext argument
        get_filename_component(WIX_TOOLS_DIR "${WIX_CANDLE}" DIRECTORY)

        add_custom_target(msi
            # Step 1: compile .wxs → .wixobj
            COMMAND "${WIX_CANDLE}" -nologo -arch x64
                -out "${CMAKE_BINARY_DIR}/vora.wixobj"
                "${CMAKE_BINARY_DIR}/vora.wxs"
            # Step 2: link .wixobj → .msi
            COMMAND "${WIX_LIGHT}" -nologo
                -ext "${WIX_TOOLS_DIR}/WixUIExtension.dll"
                -out "${CMAKE_BINARY_DIR}/vora-${PROJECT_VERSION}-Windows-AMD64.msi"
                "${CMAKE_BINARY_DIR}/vora.wixobj"
            DEPENDS Vora vora_lib ${_msi_extra_deps} "${CMAKE_BINARY_DIR}/vora.wxs"
            COMMENT "Building MSI with WiX v3 + WixUI..."
        )
        message(STATUS "MSI target enabled (candle: ${WIX_CANDLE}, light: ${WIX_LIGHT})")
    else()
        message(STATUS "MSI target disabled (WiX v3 candle/light not found)")
    endif()
endif()
