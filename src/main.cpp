#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"

#include "ast/ast_printer.h"

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

// Register built-in natives on a VM (shared between script and REPL paths).
static void registerBuiltins(VM& vm) {
    vm.defineNative("print", -1,
        [](const std::vector<Value>& arguments) -> Value {
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (i > 0) std::cout << ' ';
                printValue(arguments[i]);
            }
            std::cout << std::endl;
            return nullptr;
        });
    vm.defineNative("type", 1,
        [](const std::vector<Value>& arguments) -> Value {
            return std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
                else if constexpr (std::is_same_v<T, double>) return "float";
                else if constexpr (std::is_same_v<T, int64_t>) return "int";
                else if constexpr (std::is_same_v<T, bool>) return "boolean";
                else if constexpr (std::is_same_v<T, std::string>) return "string";
                else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) return "array";
                else if constexpr (std::is_same_v<T, std::shared_ptr<Callable>>) return "function";
                else if constexpr (std::is_same_v<T, std::shared_ptr<ObjectInstance>>) return "object";
                else return "unknown";
            }, arguments[0]);
        });
    vm.defineNative("len", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& v = arguments[0];
            if (std::holds_alternative<std::shared_ptr<Array>>(v))
                return static_cast<int64_t>(std::get<std::shared_ptr<Array>>(v)->elements.size());
            if (std::holds_alternative<std::string>(v))
                return static_cast<int64_t>(std::get<std::string>(v).size());
            return nullptr;  // will be caught by runtime
        });
    vm.defineNative("clock", 0,
        [](const std::vector<Value>&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return static_cast<double>(millis) / 1000.0;
        });
    vm.defineNative("assert", -1,
        [&vm](const std::vector<Value>& arguments) -> Value {
            if (arguments.empty()) return nullptr;
            if (!VM::isTruthy(arguments[0])) {
                std::string msg = "Assertion failed";
                if (arguments.size() >= 2 && std::holds_alternative<std::string>(arguments[1])) {
                    msg = std::get<std::string>(arguments[1]);
                }
                // Route through VM exception mechanism so assert failures
                // are catchable by try/catch (consistent with interpreter).
                vm.nativeError = true;
                vm.nativeErrorValue = std::string(msg);
                return nullptr;
            }
            return nullptr;
        });
    vm.defineNative("int", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (std::holds_alternative<int64_t>(arg))
                return arg;
            if (std::holds_alternative<double>(arg))
                return static_cast<int64_t>(std::trunc(std::get<double>(arg)));
            if (std::holds_alternative<std::string>(arg))
                return static_cast<int64_t>(std::trunc(std::stod(std::get<std::string>(arg))));
            if (std::holds_alternative<bool>(arg))
                return std::get<bool>(arg) ? static_cast<int64_t>(1) : static_cast<int64_t>(0);
            return nullptr;
        });
    vm.defineNative("float", 1,
        [](const std::vector<Value>& arguments) -> Value {
            const auto& arg = arguments[0];
            if (std::holds_alternative<int64_t>(arg)) return static_cast<double>(std::get<int64_t>(arg));
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
                end = toDouble(arguments[0]);
            } else if (arguments.size() == 2) {
                start = toDouble(arguments[0]);
                end = toDouble(arguments[1]);
            } else if (arguments.size() == 3) {
                start = toDouble(arguments[0]);
                end = toDouble(arguments[1]);
                step = toDouble(arguments[2]);
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
            int64_t n;
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
            if (n == 0) return std::string("0b0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            std::string bits;
            while (u > 0) { bits = (u & 1 ? '1' : '0') + bits; u >>= 1; }
            return (neg ? std::string("-0b") : std::string("0b")) + bits;
        });
    vm.defineNative("oct", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
            if (n == 0) return std::string("0o0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            std::string digits;
            while (u > 0) { digits = static_cast<char>('0' + (u & 7)) + digits; u >>= 3; }
            return (neg ? std::string("-0o") : std::string("0o")) + digits;
        });
    vm.defineNative("hex", 1,
        [](const std::vector<Value>& arguments) -> Value {
            int64_t n;
            if (std::holds_alternative<int64_t>(arguments[0]))
                n = std::get<int64_t>(arguments[0]);
            else
                n = static_cast<int64_t>(std::trunc(std::get<double>(arguments[0])));
            if (n == 0) return std::string("0x0");
            bool neg = n < 0;
            uint64_t u = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
            const char* hexChars = "0123456789abcdef";
            std::string digits;
            while (u > 0) { digits = hexChars[u & 0xF] + digits; u >>= 4; }
            return (neg ? std::string("-0x") : std::string("0x")) + digits;
        });
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