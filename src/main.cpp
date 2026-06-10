#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
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

#include "vm/compiler.h"
#include "vm/vm.h"

#include <ctime>

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
    bool printTokens,
    bool interpreterMode
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

    if (!interpreterMode) {
        // Bytecode VM path (default)
        Compiler compiler;
        Chunk chunk = compiler.compile(program.get());
        VM vm;

        // Pre-allocate global slots from compiler's interning table
        vm.initGlobals(compiler.getGlobalNames());

        // Register builtins
        vm.defineNative("print", -1,
            [](const std::vector<Value>& arguments) -> Value {
                for (size_t i = 0; i < arguments.size(); ++i) {
                    if (i > 0) std::cout << ' ';
                    printValue(arguments[i]);
                }
                std::cout << std::endl;
                return nullptr;
            });
        vm.defineNative("clock", 0,
            [](const std::vector<Value>&) -> Value {
                auto now = std::chrono::system_clock::now().time_since_epoch();
                auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                return static_cast<double>(millis) / 1000.0;
            });
        vm.defineNative("assert", -1,
            [](const std::vector<Value>& arguments) -> Value {
                if (arguments.empty()) return nullptr;
                if (!VM::isTruthy(arguments[0])) {
                    std::string msg = "Assertion failed";
                    if (arguments.size() >= 2 && std::holds_alternative<std::string>(arguments[1])) {
                        msg = std::get<std::string>(arguments[1]);
                    }
                    std::cerr << "VM AssertionError: " << msg << std::endl;
                    std::exit(1);
                }
                return nullptr;
            });
        vm.defineNative("int", 1,
            [](const std::vector<Value>& arguments) -> Value {
                const auto& arg = arguments[0];
                if (std::holds_alternative<double>(arg))
                    return std::trunc(std::get<double>(arg));
                if (std::holds_alternative<std::string>(arg))
                    return std::trunc(std::stod(std::get<std::string>(arg)));
                if (std::holds_alternative<bool>(arg))
                    return std::get<bool>(arg) ? 1.0 : 0.0;
                return nullptr;
            });
        vm.defineNative("float", 1,
            [](const std::vector<Value>& arguments) -> Value {
                const auto& arg = arguments[0];
                if (std::holds_alternative<double>(arg)) return arg;
                if (std::holds_alternative<std::string>(arg))
                    return std::stod(std::get<std::string>(arg));
                if (std::holds_alternative<bool>(arg))
                    return std::get<bool>(arg) ? 1.0 : 0.0;
                return nullptr;
            });
        vm.defineNative("range", -1,
            [](const std::vector<Value>& arguments) -> Value {
                double start = 0, end = 0, step = 1;
                if (arguments.size() == 1) {
                    end = std::get<double>(arguments[0]);
                } else if (arguments.size() == 2) {
                    start = std::get<double>(arguments[0]);
                    end = std::get<double>(arguments[1]);
                } else if (arguments.size() == 3) {
                    start = std::get<double>(arguments[0]);
                    end = std::get<double>(arguments[1]);
                    step = std::get<double>(arguments[2]);
                }
                auto arr = std::make_shared<Array>();
                if (step > 0) {
                    for (double i = start; i < end; i += step)
                        arr->elements.push_back(i);
                } else if (step < 0) {
                    for (double i = start; i > end; i += step)
                        arr->elements.push_back(i);
                }
                return arr;
            });
        vm.defineNative("input", -1,
            [](const std::vector<Value>& arguments) -> Value {
                if (!arguments.empty()) std::cout << valueToString(arguments[0]);
                std::string line;
                std::getline(std::cin, line);
                return line;
            });
        vm.defineNative("bin", 1,
            [](const std::vector<Value>& arguments) -> Value {
                double val = std::get<double>(arguments[0]);
                int64_t n = static_cast<int64_t>(std::trunc(val));
                if (n == 0) return std::string("0b0");
                bool neg = n < 0;
                uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
                std::string bits;
                while (u > 0) { bits = (u & 1 ? '1' : '0') + bits; u >>= 1; }
                return (neg ? std::string("-0b") : std::string("0b")) + bits;
            });
        vm.defineNative("oct", 1,
            [](const std::vector<Value>& arguments) -> Value {
                double val = std::get<double>(arguments[0]);
                int64_t n = static_cast<int64_t>(std::trunc(val));
                if (n == 0) return std::string("0o0");
                bool neg = n < 0;
                uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
                std::string digits;
                while (u > 0) { digits = static_cast<char>('0' + (u & 7)) + digits; u >>= 3; }
                return (neg ? std::string("-0o") : std::string("0o")) + digits;
            });
        vm.defineNative("hex", 1,
            [](const std::vector<Value>& arguments) -> Value {
                double val = std::get<double>(arguments[0]);
                int64_t n = static_cast<int64_t>(std::trunc(val));
                if (n == 0) return std::string("0x0");
                bool neg = n < 0;
                uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
                const char* hexChars = "0123456789abcdef";
                std::string digits;
                while (u > 0) { digits = hexChars[u & 0xF] + digits; u >>= 4; }
                return (neg ? std::string("-0x") : std::string("0x")) + digits;
            });

        // Print bytecode disassembly if requested
        if (printTokens) {
            chunk.disassemble("VM Bytecode");
        }

        InterpretResult result = vm.interpret(chunk);
        if (result != InterpretResult::OK) {
            std::exit(1);
        }
        return;
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
    bool interpreterMode = false;  // default: VM
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

        if (arg == "--interpreter") {
            interpreterMode = true;
            continue;
        }

        // --vm is now the default; recognized for compatibility
        if (arg == "--vm") {
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
            << "  vora <file> --interpreter\n"
            << "  vora <file> --ast-printer\n"
            << "  vora <file> --tokens\n"
            << "  vora <file> --tokens          (bytecode disassembly, default VM)\n"
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
        runScript(source, printAst, printTokens, interpreterMode);
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