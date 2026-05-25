cmake_minimum_required(VERSION 3.16)

project(Vora)

set(CMAKE_CXX_STANDARD 17)

add_executable(Vora
    src/main.cpp

    src/lexer/token.cpp
    src/lexer/lexer.cpp

    src/ast/expr.cpp
    src/parser/parser.cpp
    src/ast/ast_printer.cpp
)

target_include_directories(Vora PRIVATE src)
