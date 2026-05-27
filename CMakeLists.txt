cmake_minimum_required(VERSION 3.16)

project(Vora)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

    src/interpreter/interpreter.cpp
)

target_include_directories(
    Vora PRIVATE src
)

target_compile_features(
    Vora PRIVATE cxx_std_17
)
