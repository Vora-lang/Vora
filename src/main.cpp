#include <fstream>
#include <iostream>
#include <sstream>

#include "lexer/lexer.h"
#include "parser/parser.h"

#include "interpreter/interpreter.h"

int main() {

    // Read source file

    std::ifstream file(
        "D:/Vora/examples/main.va"
    );

    if (!file.is_open()) {

        std::cerr
            << "Failed to open file"
            << std::endl;

        return 1;
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    std::string source =
        buffer.str();

        if (
            source.size() >= 3
            &&
            static_cast<unsigned char>(source[0]) == 0xEF
            &&
            static_cast<unsigned char>(source[1]) == 0xBB
            &&
            static_cast<unsigned char>(source[2]) == 0xBF
        ) {
            source.erase(0, 3);
        }

    // Lexer

    vora::Lexer lexer(source);

    auto tokens =
        lexer.scanTokens();

    // Parser

    vora::Parser parser(tokens);

    auto program =
        parser.parse();

    // Interpreter

    vora::Interpreter interpreter;

    interpreter.interpret(
        program.get()
    );

    return 0;
}