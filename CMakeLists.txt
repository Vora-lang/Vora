cmake_minimum_required(VERSION 3.16)

project(Vora VERSION 0.9.0 DESCRIPTION "A dynamically-typed scripting language with bytecode VM")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ──────────────────────────────────────────────────
# Source files
# ──────────────────────────────────────────────────

add_executable(Vora

    src/main.cpp
    src/ast/ast_printer.cpp
    src/ast/program.cpp
    src/ast/expr.cpp
    src/ast/stmt.cpp

    src/lexer/token.cpp
    src/lexer/lexer.cpp

    src/parser/parser.cpp

    src/runtime/value.cpp
    src/runtime/native_function.cpp
    src/runtime/vora_function.cpp
    src/runtime/environment.cpp
    src/runtime/runtime_error.cpp

    src/interpreter/interpreter.cpp

    src/vm/chunk.cpp
    src/vm/compiler.cpp
    src/vm/vm.cpp
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

# file(GLOB) may return absolute or relative paths depending on generator.
# Use WORKING_DIRECTORY so relative paths resolve correctly, and strip the
# source-dir prefix if the path is already absolute.

# VM mode (default) tests
foreach(test_file ${TEST_LEXER} ${TEST_PARSER} ${TEST_RUNTIME} ${TEST_INTERPRETER})
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

# Interpreter mode tests
foreach(test_file ${TEST_LEXER} ${TEST_PARSER} ${TEST_RUNTIME} ${TEST_INTERPRETER})
    get_filename_component(test_name ${test_file} NAME_WLE)
    set(rel_path "${test_file}")
    if(rel_path MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}")
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" rel_path "${rel_path}")
    endif()
    add_test(
        NAME "interp-${test_name}"
        COMMAND Vora --interpreter "${rel_path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endforeach()
