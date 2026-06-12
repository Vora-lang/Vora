#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"

#include "ast/ast_printer.h"

#include "runtime/builtins.h"
#include "runtime/runtime_error.h"

#include "vm/compiler.h"
#include "vm/vm.h"

using namespace vora;

static std::string readFile(
    const std::string& path
) {

    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    return buffer.str();
}

static int runScript(
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
        return 1;
    }

    if (printAst) {
        ASTPrinter printer;
        std::cout << printer.print(program.get()) << std::endl;
    }

    // Bytecode VM path
    Compiler compiler;
    Chunk chunk = compiler.compile(program.get());

    if (compiler.hadError) {
        std::cerr << "Compilation failed" << std::endl;
        return 1;
    }

    VM vm;

    // Pre-allocate global slots from compiler's interning table
    vm.initGlobals(compiler.getGlobalNames());

    // Register builtins
    registerBuiltins(vm);

    // Print bytecode disassembly if requested
    if (printTokens) {
        chunk.disassemble("VM Bytecode");
    }

    InterpretResult result = vm.interpret(chunk);
    if (result != InterpretResult::OK) {
        // Error already printed by the VM (via runtimeError).
        return 1;
    }
    return 0;
}

static void runREPL() {
    VM vm;

    // Register builtins once — they persist across REPL lines.
    registerBuiltins(vm);

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

        Compiler compiler;
        Chunk chunk = compiler.compile(program.get());

        if (compiler.hadError) {
            continue;  // error already printed, skip interpretation
        }

        // Ensure globals referenced in this chunk have slots
        // (idempotent — existing globals are not duplicated).
        vm.initGlobals(compiler.getGlobalNames());

        InterpretResult result = vm.interpret(chunk);
        if (result == InterpretResult::RUNTIME_ERROR) {
            // Error already printed by runtimeError(); continue REPL.
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

        // --interpreter and --vm are recognized for compatibility
        // (VM is now the only backend).
        if (arg == "--interpreter" || arg == "--vm") {
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

    try {
        std::string source = readFile(path);

        if (source.size() >= 3 &&
            (unsigned char)source[0] == 0xEF &&
            (unsigned char)source[1] == 0xBB &&
            (unsigned char)source[2] == 0xBF) {
            source.erase(0, 3);
        }

        return runScript(source, printAst, printTokens);
    }
    catch (const RuntimeError& error) {
        std::cerr
            << "RuntimeError ["
            << error.line()
            << ":"
            << error.column()
            << "]: "
            << error.what()
            << std::endl;
        return 1;
    }
    catch (const std::runtime_error& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    return 0;
}