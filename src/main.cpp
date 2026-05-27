#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"

#include "ast/ast_printer.h"

#include "interpreter/interpreter.h"

using namespace vora;

static std::string readFile(
    const std::string& path
) {

    std::ifstream file(path);

    if (!file.is_open()) {

        std::cerr
            << "Failed to open file: "
            << path
            << std::endl;

        std::exit(1);
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    return buffer.str();
}

int main(
    int argc,
    char* argv[]
) {

    if (argc < 2) {

        std::cerr
            << "Usage:\n"
            << "  vora <file>\n"
            << "  vora <file> --ast-printer\n"
            << "  vora <file> --tokens\n";

        return 1;
    }

    std::string path = argv[1];

    bool printAst = false;

    bool printTokens = false;

    for (int i = 2; i < argc; i++) {

        std::string arg = argv[i];

        if (arg == "--ast-printer") {
            printAst = true;
        }

        if (arg == "--tokens") {
            printTokens = true;
        }
    }

    std::string source =
        readFile(path);

    if (source.size() >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
    
        source.erase(0, 3);
    }

    Lexer lexer(source);

    auto tokens =
        lexer.scanTokens();

    if (printTokens) {

        for (const auto& token : tokens) {

            std::cout
                << token.toString()
                << std::endl;
        }
    }

    Parser parser(tokens);

    auto program =
        parser.parse();

    if (!program) {

        std::cerr
            << "Parse failed"
            << std::endl;

        return 1;
    }

    if (printAst) {

        ASTPrinter printer;

        std::cout
            << printer.print(program.get())
            << std::endl;
    }

    Interpreter interpreter;

    interpreter.interpret(
        program.get()
    );

    return 0;
}