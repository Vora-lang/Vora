/**
 * libFuzzer harness for Vora's VM execution layer.
 *
 * Feeds arbitrary byte sequences as Vora source code through the full
 * compilation pipeline (Lexer → Parser → Compiler → VM).  All Vora-level
 * errors are swallowed silently — only memory errors (ASan), undefined
 * behaviour (UBSan), and outright crashes are reported.
 *
 * Build:
 *   cmake -DVORA_FUZZER=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
 *         -DCMAKE_BUILD_TYPE=Debug -B build/fuzz
 *   cmake --build build/fuzz
 *
 * Run:
 *   ./build/fuzz/Vora_fuzzer -max_len=65536 -runs=1000000 tests/fuzz/corpus/
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "runtime/builtins.h"
#include "runtime/value.h"
#include "vm/compiler.h"
#include "vm/vm.h"

namespace {

/**
 * Run the full Vora pipeline on a null-terminated source string.
 * Returns silently on any Vora-level error (parse, compile, runtime).
 * Only crashes if a sanitizer or the OS detects memory corruption.
 */
void runVoraPipeline(const std::string& source) {
    // ── Lex ──────────────────────────────────────────────────────────
    vora::StderrErrorReporter reporter(source);
    vora::Lexer lexer(source, reporter);
    std::vector<vora::Token> tokens;
    try {
        tokens = lexer.scanTokens();
    } catch (...) {
        return;  // lexer shouldn't throw, but be defensive
    }

    // ── Parse ────────────────────────────────────────────────────────
    vora::Parser parser(tokens, reporter);
    parser.setSource(source);
    auto program = parser.parse();
    if (!program) {
        return;  // parse error — expected, not a bug
    }

    // ── Compile ──────────────────────────────────────────────────────
    vora::Compiler compiler(reporter);
    compiler.setSource(source);
    vora::Chunk chunk = compiler.compile(program.get());
    if (compiler.hadError) {
        return;  // compile error — expected, not a bug
    }

    // ── Execute ──────────────────────────────────────────────────────
    vora::VM vm;
    vm.initGlobals(compiler.getGlobalNames());
    vora::registerBuiltins(vm);

    // interpret() catches runtime errors internally and prints them.
    // We don't care about the result — we only care about crashes.
    vm.interpret(chunk);
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
// libFuzzer entry point
// ═══════════════════════════════════════════════════════════════════════

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Reject empty or excessively large inputs early.
    if (size == 0 || size > 65536) {
        return 0;
    }

    // Null-terminate the input so we can treat it as a C-string.
    // std::string copies the data and adds a null terminator implicitly.
    std::string source(reinterpret_cast<const char*>(data), size);

    runVoraPipeline(source);
    return 0;  // no crash = success for the fuzzer
}
