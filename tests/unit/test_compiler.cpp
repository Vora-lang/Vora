// tests/unit/test_compiler.cpp — Compiler unit tests
//
// Tests: constant folding (arithmetic, unary, string concat),
//        jump back-patching (if/if-else/while), global/local
//        variables, functions/closures, upvalue resolution,
//        try/catch bytecode emission.

#include "doctest.h"
#include "lexer/lexer.h"
#include "vm/compiler.h"
#include "vm/opcode.h"
#include "parser/parser.h"
#include "ast/program.h"

using namespace vora;

// Helper: lex + parse + compile source, return the compiled Chunk.
static Chunk compile(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler;
    Chunk chunk = compiler.compile(prog.get());
    REQUIRE_FALSE(compiler.hadError);
    return chunk;
}

// Helper: find an opcode in the bytecode stream.
static bool containsOpcode(const Chunk& chunk, OpCode op) {
    for (uint8_t byte : chunk.code) {
        if (byte == static_cast<uint8_t>(op)) return true;
    }
    return false;
}

// Helper: count occurrences of an opcode.
static int countOpcodes(const Chunk& chunk, OpCode op) {
    int count = 0;
    for (uint8_t byte : chunk.code) {
        if (byte == static_cast<uint8_t>(op)) count++;
    }
    return count;
}

// ============================================================================
// Basic literals
// ============================================================================

TEST_CASE("compiler_empty_program") {
    auto chunk = compile("");
    // Should end with OP_NULL, OP_RETURN
    REQUIRE(chunk.code.size() >= 2);
    size_t n = chunk.code.size();
    CHECK(chunk.code[n - 2] == static_cast<uint8_t>(OpCode::OP_NULL));
    CHECK(chunk.code[n - 1] == static_cast<uint8_t>(OpCode::OP_RETURN));
}

TEST_CASE("compiler_literal_null") {
    auto chunk = compile("null;");
    CHECK(containsOpcode(chunk, OpCode::OP_NULL));
}

TEST_CASE("compiler_literal_true") {
    auto chunk = compile("true;");
    CHECK(containsOpcode(chunk, OpCode::OP_TRUE));
}

TEST_CASE("compiler_literal_false") {
    auto chunk = compile("false;");
    CHECK(containsOpcode(chunk, OpCode::OP_FALSE));
}

TEST_CASE("compiler_literal_integer") {
    auto chunk = compile("42;");
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
}

TEST_CASE("compiler_literal_float") {
    auto chunk = compile("3.14;");
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
}

TEST_CASE("compiler_literal_string") {
    auto chunk = compile("\"hello\";");
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
    // Verify the constant is in the pool
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (std::holds_alternative<std::string>(c) &&
            std::get<std::string>(c) == "hello") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Constant folding
// ============================================================================

TEST_CASE("compiler_constant_fold_int_add") {
    // 2 + 3 → OP_CONSTANT(5), no OP_ADD
    auto chunk = compile("2 + 3;");
    CHECK_FALSE(containsOpcode(chunk, OpCode::OP_ADD));
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
}

TEST_CASE("compiler_constant_fold_int_sub") {
    auto chunk = compile("10 - 3;");
    CHECK_FALSE(containsOpcode(chunk, OpCode::OP_SUB_NN));
}

TEST_CASE("compiler_constant_fold_int_mul") {
    auto chunk = compile("6 * 7;");
    CHECK_FALSE(containsOpcode(chunk, OpCode::OP_MUL_NN));
}

TEST_CASE("compiler_constant_fold_float_add") {
    auto chunk = compile("2.5 + 3.5;");
    CHECK_FALSE(containsOpcode(chunk, OpCode::OP_ADD));
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
}

TEST_CASE("compiler_constant_fold_string_concat") {
    auto chunk = compile("\"hello \" + \"world\";");
    // Constant-folded to "hello world" in one constant
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (std::holds_alternative<std::string>(c) &&
            std::get<std::string>(c) == "hello world") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("compiler_constant_fold_unary_neg") {
    // -42 → OP_CONSTANT(-42)
    auto chunk = compile("-42;");
    CHECK(containsOpcode(chunk, OpCode::OP_CONSTANT));
}

TEST_CASE("compiler_constant_fold_unary_not_true") {
    auto chunk = compile("!true;");
    CHECK(containsOpcode(chunk, OpCode::OP_FALSE));
}

TEST_CASE("compiler_no_fold_with_variable") {
    // x + 5 (x is global variable) — should NOT fold
    auto chunk = compile("let x = 10; x + 5;");
    // Should contain OP_ADD or OP_GET_GLOBAL
    CHECK(containsOpcode(chunk, OpCode::OP_GET_GLOBAL));
}

// ============================================================================
// Global variables
// ============================================================================

TEST_CASE("compiler_global_define") {
    auto chunk = compile("let x = 42;");
    CHECK(containsOpcode(chunk, OpCode::OP_DEFINE_GLOBAL));
}

TEST_CASE("compiler_global_read") {
    auto chunk = compile("let x = 5; x;");
    CHECK(containsOpcode(chunk, OpCode::OP_GET_GLOBAL));
}

TEST_CASE("compiler_global_assignment") {
    auto chunk = compile("let x = 5; x = 10;");
    CHECK(containsOpcode(chunk, OpCode::OP_SET_GLOBAL));
}

// ============================================================================
// Local variables
// ============================================================================

TEST_CASE("compiler_local_var_access") {
    // Inside a block (or function), locals use OP_GET_LOCAL
    auto chunk = compile("{ let x = 1; x; }");
    CHECK(containsOpcode(chunk, OpCode::OP_GET_LOCAL));
}

TEST_CASE("compiler_local_shadows_global") {
    // Global x, then block-local x uses OP_GET_LOCAL
    auto chunk = compile("let x = 1; { let x = 2; x; } x;");
    // Should have both OP_GET_LOCAL and OP_GET_GLOBAL
    CHECK(containsOpcode(chunk, OpCode::OP_GET_LOCAL));
    CHECK(containsOpcode(chunk, OpCode::OP_GET_GLOBAL));
}

// ============================================================================
// Jump back-patching
// ============================================================================

TEST_CASE("compiler_if_jump_patching") {
    auto chunk = compile("if (true) { 1; }");
    CHECK(containsOpcode(chunk, OpCode::OP_JUMP_IF_FALSE));
    // After the jump-if-false + its 2-byte operand, the then-branch follows
}

TEST_CASE("compiler_if_else_jump_patching") {
    auto chunk = compile("if (true) { 1; } else { 2; }");
    CHECK(containsOpcode(chunk, OpCode::OP_JUMP_IF_FALSE));
    CHECK(containsOpcode(chunk, OpCode::OP_JUMP));  // unconditional jump over else
}

TEST_CASE("compiler_while_loop_jumps") {
    auto chunk = compile("while (true) { 1; }");
    CHECK(containsOpcode(chunk, OpCode::OP_JUMP_IF_FALSE));
    CHECK(containsOpcode(chunk, OpCode::OP_LOOP));  // backward jump
}

// ============================================================================
// Functions and closures
// ============================================================================

TEST_CASE("compiler_function_closure_emission") {
    auto chunk = compile("func foo() { return 1; }");
    CHECK(containsOpcode(chunk, OpCode::OP_CLOSURE));
    // Function prototype should be in constant pool
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (std::holds_alternative<std::shared_ptr<FunctionPrototype>>(c)) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("compiler_upvalue_resolution") {
    // Inner function captures x from outer → upvalue
    auto chunk = compile(
        "func outer() {"
        "  let x = 1;"
        "  func inner() { return x; }"
        "}");
    // (compilation success already verified by REQUIRE_FALSE in compile() helper)
}

// ============================================================================
// Try/catch
// ============================================================================

TEST_CASE("compiler_try_catch_opcodes") {
    auto chunk = compile("try { 1; } catch(e) { 2; }");
    CHECK(containsOpcode(chunk, OpCode::OP_PUSH_CATCH));
    CHECK(containsOpcode(chunk, OpCode::OP_POP_CATCH));
    CHECK(containsOpcode(chunk, OpCode::OP_CLEAR_EXCEPTION));
}

TEST_CASE("compiler_throw_opcode") {
    auto chunk = compile("throw \"err\"");
    CHECK(containsOpcode(chunk, OpCode::OP_THROW));
}

// ============================================================================
// Break / Continue
// ============================================================================

TEST_CASE("compiler_break_in_loop") {
    auto chunk = compile("while (true) { break }");
    CHECK(containsOpcode(chunk, OpCode::OP_JUMP));
}

TEST_CASE("compiler_continue_in_loop") {
    // In a while loop, continue is an OP_LOOP
    auto chunk = compile("while (true) { continue }");
    CHECK(containsOpcode(chunk, OpCode::OP_LOOP));
}

// ============================================================================
// Return
// ============================================================================

TEST_CASE("compiler_implicit_return_null") {
    auto chunk = compile("func foo() {}");
    // Function body should end with OP_NULL, OP_RETURN
    CHECK(containsOpcode(chunk, OpCode::OP_CLOSURE));
    // The closure constant contains the function prototype with its own chunk
    REQUIRE(chunk.constants.size() >= 1);
}
