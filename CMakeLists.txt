cmake_minimum_required(VERSION 3.16)

project(Nyra)

set(CMAKE_CXX_STANDARD 17)

add_executable(Nyra
    src/main.cpp

    src/lexer/token.cpp
    src/lexer/lexer.cpp
)

target_include_directories(Nyra PRIVATE src)
