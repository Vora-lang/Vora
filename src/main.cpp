#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "common/error_reporter.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

#include "ast/ast_printer.h"

#include "formatter/formatter.h"

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

static int runFmt(
    const std::string& source,
    const std::string& path,
    bool writeBack
) {
    StderrErrorReporter reporter(source);
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();

    Parser parser(tokens, reporter);
    parser.setSource(source);
    auto program = parser.parse();

    if (parser.hasError()) {
        std::cerr << "vora fmt: parse had errors for " << path << std::endl;
        // Continue anyway — format the partial AST for best-effort output.
    }

    SourceFormatter formatter;
    std::string formatted = formatter.format(program.get());

    if (writeBack) {
        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "vora fmt: failed to write to " << path << std::endl;
            return 1;
        }
        out << formatted;
        return 0;
    }

    std::cout << formatted;
    return 0;
}

// Get the directory part of a file path (everything before the last / or \).
static std::string dirname(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    return ".";
}

// Determine the std/ library directory.
static std::string findStdDir() {
    // Allow override via environment variable
    const char* env = std::getenv("VORA_STD_PATH");
    if (env && env[0] != '\0') return env;
    // Default: relative to current working directory
    return "./std";
}

static int runScript(
    const std::string& source,
    bool printAst,
    bool printTokens,
    const std::string& filePath = ""
) {
    StderrErrorReporter reporter(source);
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();

    if (printTokens) {
        for (const auto& token : tokens) {
            std::cout << token.toString() << std::endl;
        }
    }

    Parser parser(tokens, reporter);
    parser.setSource(source);
    auto program = parser.parse();

    if (parser.hasError()) {
        std::cerr << "Parse errors detected" << std::endl;
        return 1;
    }

    if (printAst) {
        ASTPrinter printer;
        std::cout << printer.print(program.get()) << std::endl;
    }

    // Bytecode VM path
    Compiler compiler(reporter);
    compiler.setSource(source);
    Chunk chunk = compiler.compile(program.get());

    if (compiler.hadError) {
        std::cerr << "Compilation failed" << std::endl;
        return 1;
    }

    VM vm;
    vm.errorReporter = &reporter;

    // Set module resolution directories
    vm.stdDir = findStdDir();
    vm.currentModuleDir = filePath.empty() ? "." : dirname(filePath);

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

// Count net open braces ({ minus }) in a line, ignoring those inside
// strings, line comments, and block comments.
static int netBraces(const std::string& s) {
    int depth = 0;
    bool inString = false;
    char stringChar = 0;
    bool inLineComment = false;
    bool inBlockComment = false;

    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        char next = (i + 1 < s.size()) ? s[i + 1] : 0;

        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') { inBlockComment = false; i++; }
            continue;
        }
        if (inString) {
            if (c == '\\') { i++; continue; }   // skip escaped char
            if (c == stringChar) inString = false;
            continue;
        }

        if (c == '/' && next == '/') { inLineComment = true; i++; continue; }
        if (c == '/' && next == '*') { inBlockComment = true; i++; continue; }
        if (c == '"' || c == '\'')  { inString = true; stringChar = c; continue; }

        if (c == '{') depth++;
        if (c == '}') depth--;
    }
    return depth;
}

// Signal handler for Ctrl+C (SIGINT).  Delegates to VM::requestInterrupt()
// so the running bytecode loop can stop cleanly at the next opcode boundary
// instead of killing the whole process.
static void replSigintHandler(int /*signum*/) {
    VM::requestInterrupt();
    // Re-register — some platforms reset to SIG_DFL after delivery.
    std::signal(SIGINT, replSigintHandler);
}

static void runREPL() {
    VM vm;
    // REPL runtime errors go to stderr (no per-line reporter; the VM's
    // errorReporter stays null, so runtimeError falls back to stderr).

    // Set module resolution directories for REPL
    vm.stdDir = findStdDir();
    vm.currentModuleDir = ".";

    // Install Ctrl+C handler so SIGINT interrupts only the running code,
    // not the entire REPL process.
    std::signal(SIGINT, replSigintHandler);

    // Register builtins once — they persist across REPL lines.
    registerBuiltins(vm);

    std::string line;
    std::string accumulated;
    int braceDepth = 0;
    const char* prompt = "> ";

    while (true) {
        std::cout << prompt;

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (accumulated.empty() && line.empty()) {
            continue;
        }

        // Append this line to the accumulated source.
        if (accumulated.empty()) {
            accumulated = line;
        } else {
            accumulated += "\n" + line;
        }

        braceDepth += netBraces(line);

        // Still inside unclosed braces — keep reading.
        if (braceDepth > 0) {
            prompt = "... ";
            continue;
        }

        // Balanced — reset state for next round.
        prompt = "> ";
        braceDepth = 0;

        std::string source = std::move(accumulated);
        accumulated.clear();

        StderrErrorReporter reporter(source);
        Lexer lexer(source, reporter);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens, reporter);
        parser.setSource(source);
        auto program = parser.parse();

        if (parser.hasError()) {
            // Skip execution of errored input in REPL.
            continue;
        }

        Compiler compiler(reporter);
        compiler.setSource(source);
        compiler.replMode = true;  // print expression results in REPL

        // Seed the compiler with the VM's current global table so that
        // slot assignments match. Without this, a `let x = 5` line and
        // a later `print(x)` line would disagree on slot numbers.
        compiler.seedGlobals(vm.getGlobalNames());

        Chunk chunk = compiler.compile(program.get());

        if (compiler.hadError) {
            continue;  // error already printed, skip interpretation
        }

        // Merge any new globals into the VM (idempotent — existing
        // globals and their values are preserved).
        vm.initGlobals(compiler.getGlobalNames());

        InterpretResult result = vm.interpret(chunk);
        if (result == InterpretResult::RUNTIME_ERROR) {
            // Error already printed by runtimeError(); continue REPL.
            // (Interrupted runs also return RUNTIME_ERROR — "Interrupted"
            //  is printed by the VM before returning.)
        }
    }

    // Restore default signal handling before exiting REPL.
    std::signal(SIGINT, SIG_DFL);
}

int main(
    int argc,
    char* argv[]
) {

    bool printAst = false;
    bool printTokens = false;
    bool repl = false;
    bool fmtMode = false;
    bool fmtWrite = false;
    std::string path;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "fmt") {
            fmtMode = true;
            continue;
        }

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

        if (arg == "-w" || arg == "--write") {
            fmtWrite = true;
            continue;
        }

        if (arg == "-h" || arg == "--help") {
            std::cout
                << "Vora " << VORA_VERSION << " — a dynamically typed scripting language\n"
                << "      JavaScript-like syntax, Lua-level simplicity,\n"
                << "      Wren-style object orientation.\n"
                << "\n"
                << "用法:\n"
                << "  vora                 进入 REPL 交互模式\n"
                << "  vora <文件>          执行脚本文件\n"
                << "  vora [选项] <文件>   带选项执行脚本\n"
                << "  vora fmt [-w] <文件> 格式化源文件\n"
                << "\n"
                << "选项:\n"
                << "  --ast-printer        打印 AST 并退出\n"
                << "  --tokens             打印词法单元与字节码并退出\n"
                << "  --repl               显式进入 REPL\n"
                << "  -v, -V, --version    输出版本信息并退出\n"
                << "  -h, --help           输出此帮助信息并退出\n";
            return 0;
        }

        if (arg == "-v" || arg == "-V" || arg == "--version") {
            std::cout << "Vora " << VORA_VERSION << "\n";
            return 0;
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

    if (fmtMode) {
        if (path.empty()) {
            std::cerr
                << "Usage:\n"
                << "  vora fmt <file>          format to stdout\n"
                << "  vora fmt -w <file>       format file in-place\n";
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

            return runFmt(source, path, fmtWrite);
        }
        catch (const std::runtime_error& error) {
            std::cerr << error.what() << std::endl;
            return 1;
        }
    }

    if (repl) {
        runREPL();
        return 0;
    }

    if (path.empty()) {
        // No file given — enter REPL (like python3, node, etc.)
        runREPL();
        return 0;
    }

    try {
        std::string source = readFile(path);

        if (source.size() >= 3 &&
            (unsigned char)source[0] == 0xEF &&
            (unsigned char)source[1] == 0xBB &&
            (unsigned char)source[2] == 0xBF) {
            source.erase(0, 3);
        }

        return runScript(source, printAst, printTokens, path);
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