#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"

#include "ast/ast_printer.h"

#include "interpreter/interpreter.h"
#include "runtime/runtime_config.h"
#include "runtime/runtime_error.h"

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

static void runScript(
    const std::string& source,
    bool printAst,
    bool printTokens
) {
    Lexer lexer(source);
    auto tokens = lexer.scanTokens();

    if (printTokens) {
        for (const auto& token : tokens) {
            std::cout << token.toString() << std::endl;
        }
    }

    Parser parser(tokens);
    auto program = parser.parse();

    if (!program) {
        std::cerr << "Parse failed" << std::endl;
        return;
    }

    if (printAst) {
        ASTPrinter printer;
        std::cout << printer.print(program.get()) << std::endl;
    }

    Interpreter interpreter(RuntimeConfig{RuntimeMode::Script});
    interpreter.interpret(program.get());
}

static void runREPL() {
    Interpreter interpreter(RuntimeConfig{RuntimeMode::REPL});

    std::string line;

    while (true) {
        std::cout << "> ";

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            continue;
        }

        Lexer lexer(line);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens);
        auto program = parser.parse();

        if (!program) {
            continue;
        }

        try {
            interpreter.interpret(program.get());
        }
        catch (const RuntimeError& error) {
            std::cerr
                << "RuntimeError ["
                << error.line()
                << "]: "
                << error.what()
                << std::endl;
        }
    }
}

int main(
    int argc,
    char* argv[]
) {

    bool printAst = false;
    bool printTokens = false;
    bool repl = false;
    std::string path;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--ast-printer") {
            printAst = true;
            continue;
        }

        if (arg == "--tokens") {
            printTokens = true;
            continue;
        }

        if (arg == "--repl") {
            repl = true;
            continue;
        }

        if (path.empty()) {
            path = arg;
            continue;
        }

        std::cerr
            << "Unknown argument: "
            << arg
            << std::endl;

        return 1;
    }

    if (repl) {
        runREPL();
        return 0;
    }

    if (path.empty()) {
        std::cerr
            << "Usage:\n"
            << "  vora <file>\n"
            << "  vora <file> --ast-printer\n"
            << "  vora <file> --tokens\n"
            << "  vora --repl\n";

        return 1;
    }

    std::string source =
        readFile(path);

    if (source.size() >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        source.erase(0, 3);
    }

    try {
        runScript(source, printAst, printTokens);
    }
    catch (const RuntimeError& error) {
        std::cerr
            << "RuntimeError ["
            << error.line()
            << "]: "
            << error.what()
            << std::endl;
        return 1;
    }

    return 0;
}