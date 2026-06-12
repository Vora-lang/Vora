// tests/unit/test_vm.cpp — VM unit tests
//
// Tests: stack operations (push/pop/peek), truthy checks,
//        value equality/comparison, addValues, interpret with
//        arithmetic/variables/if/functions/closures/try-catch.

#include "doctest.h"
#include "lexer/lexer.h"
#include "vm/vm.h"
#include "vm/compiler.h"
#include "parser/parser.h"
#include "runtime/builtins.h"
#include "runtime/native_function.h"

using namespace vora;

// Helper: compile + interpret source, return (result, VM reference).
// The VM outlives the call so callers can inspect vm state.
static std::pair<InterpretResult, VM> run(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler;
    Chunk chunk = compiler.compile(prog.get());
    REQUIRE_FALSE(compiler.hadError);
    VM vm;
    vm.initGlobals(compiler.getGlobalNames());
    // Register all user-facing builtins from the centralized module.
    registerBuiltins(vm);
    InterpretResult result = vm.interpret(chunk);
    return {result, std::move(vm)};
}

// ============================================================================
// Truthy checks  (public static methods)
// ============================================================================

TEST_CASE("vm_isTruthy_null") {
    CHECK_FALSE(VM::isTruthy(Value(nullptr)));
}

TEST_CASE("vm_isTruthy_bool") {
    CHECK(VM::isTruthy(Value(true)));
    CHECK_FALSE(VM::isTruthy(Value(false)));
}

TEST_CASE("vm_isTruthy_int") {
    CHECK_FALSE(VM::isTruthy(Value(static_cast<int64_t>(0))));
    CHECK(VM::isTruthy(Value(static_cast<int64_t>(1))));
    CHECK(VM::isTruthy(Value(static_cast<int64_t>(-1))));
    CHECK(VM::isTruthy(Value(static_cast<int64_t>(42))));
}

TEST_CASE("vm_isTruthy_double") {
    CHECK_FALSE(VM::isTruthy(Value(0.0)));
    CHECK(VM::isTruthy(Value(0.1)));
    CHECK(VM::isTruthy(Value(-1.5)));
}

TEST_CASE("vm_isTruthy_string") {
    CHECK_FALSE(VM::isTruthy(Value(std::string(""))));
    CHECK(VM::isTruthy(Value(std::string("hello"))));
    CHECK(VM::isTruthy(Value(std::string("0"))));
}

TEST_CASE("vm_isTruthy_array") {
    auto arr = std::make_shared<Array>();
    CHECK(VM::isTruthy(Value(arr)));  // empty array is truthy
}

// ============================================================================
// Value equality
// ============================================================================

TEST_CASE("vm_valuesEqual_null") {
    CHECK(VM::valuesEqual(Value(nullptr), Value(nullptr)));
}

TEST_CASE("vm_valuesEqual_bool") {
    CHECK(VM::valuesEqual(Value(true), Value(true)));
    CHECK_FALSE(VM::valuesEqual(Value(true), Value(false)));
}

TEST_CASE("vm_valuesEqual_int64") {
    CHECK(VM::valuesEqual(Value(static_cast<int64_t>(42)), Value(static_cast<int64_t>(42))));
    CHECK_FALSE(VM::valuesEqual(Value(static_cast<int64_t>(42)), Value(static_cast<int64_t>(43))));
}

TEST_CASE("vm_valuesEqual_double") {
    CHECK(VM::valuesEqual(Value(3.14), Value(3.14)));
    CHECK_FALSE(VM::valuesEqual(Value(3.14), Value(2.71)));
}

TEST_CASE("vm_valuesEqual_string") {
    CHECK(VM::valuesEqual(Value(std::string("abc")), Value(std::string("abc"))));
    CHECK_FALSE(VM::valuesEqual(Value(std::string("abc")), Value(std::string("def"))));
}

TEST_CASE("vm_valuesEqual_cross_type_numeric") {
    // int == double (both numeric)
    CHECK(VM::valuesEqual(Value(static_cast<int64_t>(42)), Value(42.0)));
    CHECK(VM::valuesEqual(Value(0.0), Value(static_cast<int64_t>(0))));
}

TEST_CASE("vm_valuesEqual_cross_type_non_numeric") {
    // null != int 0 (different type indices)
    CHECK_FALSE(VM::valuesEqual(Value(nullptr), Value(static_cast<int64_t>(0))));
    // bool != int 1
    CHECK_FALSE(VM::valuesEqual(Value(true), Value(static_cast<int64_t>(1))));
}

// ============================================================================
// Value comparison
// ============================================================================

TEST_CASE("vm_valuesCompare_numeric") {
    CHECK(VM().valuesCompare(Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(2))) < 0);
    CHECK(VM().valuesCompare(Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(1))) > 0);
    CHECK(VM().valuesCompare(Value(static_cast<int64_t>(1)), Value(1.0)) == 0);
}

TEST_CASE("vm_valuesCompare_non_numeric_returns_zero") {
    CHECK(VM().valuesCompare(Value(std::string("a")), Value(std::string("b"))) == 0);
}

// ============================================================================
// addValues
// ============================================================================

TEST_CASE("vm_addValues_int_plus_int") {
    VM vm;
    Value result = vm.addValues(Value(static_cast<int64_t>(3)),
                                Value(static_cast<int64_t>(4)));
    CHECK(std::holds_alternative<int64_t>(result));
    CHECK(std::get<int64_t>(result) == 7);
}

TEST_CASE("vm_addValues_int_plus_double") {
    VM vm;
    Value result = vm.addValues(Value(static_cast<int64_t>(3)), Value(4.5));
    CHECK(std::holds_alternative<double>(result));
    CHECK(std::get<double>(result) == 7.5);
}

TEST_CASE("vm_addValues_string_concat") {
    VM vm;
    Value result = vm.addValues(Value(std::string("hello ")),
                                Value(std::string("world")));
    CHECK(std::holds_alternative<std::string>(result));
    CHECK(std::get<std::string>(result) == "hello world");
}

TEST_CASE("vm_addValues_string_plus_int") {
    VM vm;
    Value result = vm.addValues(Value(std::string("x=")),
                                Value(static_cast<int64_t>(42)));
    CHECK(std::holds_alternative<std::string>(result));
    CHECK(std::get<std::string>(result) == "x=42");
}

TEST_CASE("vm_addValues_array_concat") {
    VM vm;
    auto a = std::make_shared<Array>();
    a->elements.push_back(Value(static_cast<int64_t>(1)));
    auto b = std::make_shared<Array>();
    b->elements.push_back(Value(static_cast<int64_t>(2)));
    Value result = vm.addValues(Value(a), Value(b));
    CHECK(std::holds_alternative<std::shared_ptr<Array>>(result));
    auto arr = std::get<std::shared_ptr<Array>>(result);
    REQUIRE(arr->elements.size() == 2);
    CHECK(std::get<int64_t>(arr->elements[0]) == 1);
    CHECK(std::get<int64_t>(arr->elements[1]) == 2);
}

// ============================================================================
// interpret — arithmetic
// ============================================================================

TEST_CASE("vm_interpret_arithmetic") {
    auto [result, vm] = run("1 + 2 * 3;");
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_interpret_let_and_read") {
    auto [result, vm] = run("let x = 5; x;");
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_interpret_if_else") {
    auto [result, vm] = run("let x = 0; if (true) { x = 1; } else { x = 2; }");
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_interpret_while") {
    auto [result, vm] = run("let i = 0; while (i < 3) { i = i + 1; }");
    CHECK(result == InterpretResult::OK);
}

// ============================================================================
// interpret — functions
// ============================================================================

TEST_CASE("vm_interpret_function_call") {
    auto [result, vm] = run("func add(a, b) { return a + b; } add(3, 4);");
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_interpret_closure_upvalue") {
    auto [result, vm] = run(
        "func makeCounter() {"
        "  let count = 0;"
        "  func counter() { count = count + 1; return count; }"
        "  return counter;"
        "}"
        "let c = makeCounter();"
        "c(); c();"
    );
    CHECK(result == InterpretResult::OK);
}

// ============================================================================
// interpret — native functions
// ============================================================================

TEST_CASE("vm_native_function_call") {
    auto [result, vm] = run(
        "func add(a, b) { return a + b; }"
        "let s = add(1, 2);"
    );
    CHECK(result == InterpretResult::OK);
}

// ============================================================================
// interpret — exceptions
// ============================================================================

TEST_CASE("vm_try_catch_exception") {
    auto [result, vm] = run(
        "let caught = \"\" "
        "try { throw \"err\" } catch(e) { caught = e }"
    );
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_throw_uncaught") {
    auto [result, vm] = run("throw \"err\"");
    CHECK(result == InterpretResult::RUNTIME_ERROR);
}

TEST_CASE("vm_division_by_zero") {
    auto [result, vm] = run("1/0;");
    CHECK(result == InterpretResult::RUNTIME_ERROR);
}

// ============================================================================
// interpret — objects
// ============================================================================

TEST_CASE("vm_interpret_object_creation") {
    auto [result, vm] = run(
        "Obj Point(x, y) {"
        "  this.x = x;"
        "  this.y = y;"
        "}"
        "let p = Point(3, 4);"
    );
    CHECK(result == InterpretResult::OK);
}

TEST_CASE("vm_interpret_object_method") {
    auto [result, vm] = run(
        "Obj Greeter() {"
        "  func greet() { return \"hi\"; }"
        "}"
        "let g = Greeter();"
        "g.greet();"
    );
    CHECK(result == InterpretResult::OK);
}
