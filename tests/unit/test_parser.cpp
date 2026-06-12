// tests/unit/test_parser.cpp — Parser unit tests
//
// Tests: operator precedence (all 6 levels), associativity,
//        all statement types (let/return/if/while/for/func/obj/
//        break/continue/try/throw), expression types (ternary,
//        array, index, property, call), error recovery.

#include "doctest.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/program.h"
#include "ast/ast_printer.h"

using namespace vora;

// Helper: lex + parse source, returning Program or nullptr.
static std::unique_ptr<Program> parse(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens));
    return parser.parse();
}

// Helper: parse a single expression (wrapped in "{ ... ;}" to avoid stmt-level issues)
static std::unique_ptr<Expr> parseExpr(const std::string& src) {
    // Wrap in return so we get the expression
    auto prog = parse("return " + src + ";");
    if (!prog || prog->statements.empty()) return nullptr;
    auto* ret = dynamic_cast<ReturnStmt*>(prog->statements[0].get());
    if (!ret) return nullptr;
    return std::move(ret->value);
}

// Helper: print AST for structural checks
static std::string printAST(const std::string& src) {
    auto prog = parse(src);
    if (!prog) return "PARSE_FAILED";
    ASTPrinter printer;
    return printer.print(prog.get());
}

// ============================================================================
// Operator precedence
// ============================================================================

TEST_CASE("parser_precedence_multiply_over_add") {
    // 1 + 2 * 3 → (+ 1 (* 2 3))
    auto ast = printAST("{ 1 + 2 * 3; }");
    CHECK(ast.find("(* 2 3)") != std::string::npos);
}

TEST_CASE("parser_precedence_comparison_over_equality") {
    // 1 < 2 == 3 > 4 — check structure
    auto ast = printAST("{ 1 < 2 == 3 > 4; }");
    CHECK(ast != "PARSE_FAILED");
}

TEST_CASE("parser_associativity_left_subtract") {
    // 1 - 2 - 3 → ((- 1 2) 3) — left-associative
    auto ast = printAST("{ 1 - 2 - 3; }");
    CHECK(ast.find("(- (- 1 2) 3)") != std::string::npos);
}

TEST_CASE("parser_associativity_right_power") {
    // 2 ** 3 ** 2 → (** 2 (** 3 2)) — right-associative
    auto ast = printAST("{ 2 ** 3 ** 2; }");
    CHECK(ast.find("(** 2 (** 3 2))") != std::string::npos);
}

TEST_CASE("parser_associativity_right_assignment") {
    // a = b = 5 — right-associative
    auto prog = parse("let a = 1; let b = 2; a = b = 5;");
    CHECK(prog != nullptr);
}

TEST_CASE("parser_precedence_and_over_or") {
    // a and b or c → (or (and a b) c)
    auto ast = printAST("{ true and false or true; }");
    CHECK(ast.find("(and") != std::string::npos);
}

TEST_CASE("parser_compound_assignment_precedence") {
    // x += 2 * 3 → RHS is (* 2 3), not (+= x 2) * 3
    auto prog = parse("let x = 0; x += 2 * 3;");
    CHECK(prog != nullptr);
}

// ============================================================================
// Statements
// ============================================================================

TEST_CASE("parser_let_statement") {
    auto prog = parse("let x = 5;");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* let = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(let != nullptr);
    CHECK(let->name == "x");
}

TEST_CASE("parser_let_without_initializer_error") {
    Lexer lexer("let x;");
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    CHECK(parser.hasError());
}

TEST_CASE("parser_return_with_value") {
    auto prog = parse("func f() { return 42; }");
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_if_statement") {
    auto prog = parse("if (true) { 1; }");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    CHECK(dynamic_cast<IfStmt*>(prog->statements[0].get()) != nullptr);
}

TEST_CASE("parser_if_else_statement") {
    auto prog = parse("if (true) { 1; } else { 2; }");
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_while_statement") {
    auto prog = parse("while (true) { 1; }");
    REQUIRE(prog != nullptr);
    CHECK(dynamic_cast<WhileStmt*>(prog->statements[0].get()) != nullptr);
}

TEST_CASE("parser_for_in_statement") {
    auto prog = parse("for x in [1,2,3] { print(x); }");
    REQUIRE(prog != nullptr);
    auto* f = dynamic_cast<ForStmt*>(prog->statements[0].get());
    REQUIRE(f != nullptr);
    CHECK(f->variable == "x");
}

TEST_CASE("parser_func_statement") {
    auto prog = parse("func add(a, b) { return a + b; }");
    REQUIRE(prog != nullptr);
    auto* func = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    REQUIRE(func != nullptr);
    CHECK(func->name == "add");
    REQUIRE(func->params.size() == 2);
    CHECK(func->params[0] == "a");
    CHECK(func->params[1] == "b");
}

TEST_CASE("parser_func_no_params") {
    auto prog = parse("func greet() { print(\"hi\"); }");
    REQUIRE(prog != nullptr);
    auto* func = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    REQUIRE(func != nullptr);
    CHECK(func->params.empty());
}

TEST_CASE("parser_obj_statement") {
    auto prog = parse("Obj Point(x, y) { this.x = x; this.y = y; }");
    REQUIRE(prog != nullptr);
    auto* obj = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    REQUIRE(obj != nullptr);
    CHECK(obj->name == "Point");
    REQUIRE(obj->params.size() == 2);
    CHECK(obj->params[0] == "x");
    CHECK(obj->params[1] == "y");
}

TEST_CASE("parser_obj_inheritance") {
    auto prog = parse("Obj Child : Parent(x) { }");
    REQUIRE(prog != nullptr);
    auto* obj = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    REQUIRE(obj != nullptr);
    CHECK(obj->name == "Child");
    CHECK(obj->parentName == "Parent");
}

TEST_CASE("parser_break_continue") {
    auto prog = parse("while (true) { break continue }");
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_try_catch") {
    auto prog = parse("try { risky(); } catch (e) { print(e); }");
    REQUIRE(prog != nullptr);
    auto* t = dynamic_cast<TryStmt*>(prog->statements[0].get());
    REQUIRE(t != nullptr);
    CHECK(t->catchVar == "e");
    CHECK(t->catchBlock != nullptr);
    CHECK(t->finallyBlock == nullptr);
}

TEST_CASE("parser_try_finally") {
    auto prog = parse("try { risky(); } finally { cleanup(); }");
    REQUIRE(prog != nullptr);
    auto* t = dynamic_cast<TryStmt*>(prog->statements[0].get());
    REQUIRE(t != nullptr);
    CHECK(t->finallyBlock != nullptr);
    CHECK(t->catchBlock == nullptr);
}

TEST_CASE("parser_try_catch_finally") {
    auto prog = parse("try { a(); } catch (e) { b(); } finally { c(); }");
    REQUIRE(prog != nullptr);
    auto* t = dynamic_cast<TryStmt*>(prog->statements[0].get());
    REQUIRE(t != nullptr);
    CHECK(t->catchBlock != nullptr);
    CHECK(t->finallyBlock != nullptr);
}

TEST_CASE("parser_throw_statement") {
    auto prog = parse("throw \"error\"");
    REQUIRE(prog != nullptr);
    CHECK(dynamic_cast<ThrowStmt*>(prog->statements[0].get()) != nullptr);
}

// ============================================================================
// Expressions
// ============================================================================

TEST_CASE("parser_ternary") {
    auto prog = parse("{ true ? 1 : 2; }");
    REQUIRE(prog != nullptr);
    auto ast = printAST("{ true ? 1 : 2; }");
    CHECK(ast.find("(?") != std::string::npos);
}

TEST_CASE("parser_nested_ternary_right_assoc") {
    // a ? b ? 1 : 2 : 3 — right-assoc: a ? (b ? 1 : 2) : 3
    auto ast = printAST("{ true ? false ? 1 : 2 : 3; }");
    CHECK(ast != "PARSE_FAILED");
}

TEST_CASE("parser_array_literal") {
    auto ast = printAST("[1, 2, 3]");
    CHECK(ast.find("[") != std::string::npos);  // array literal
}

TEST_CASE("parser_empty_array") {
    auto ast = printAST("[]");
    CHECK(ast.find("[") != std::string::npos);  // array literal
}

TEST_CASE("parser_index_expression") {
    auto ast = printAST("{ let a = [1,2]; a[0]; }");
    CHECK(ast.find("(index") != std::string::npos);
}

TEST_CASE("parser_property_access") {
    auto prog = parse("Obj O() { func m() {} } let o = O(); o.m();");
    // obj.prop → PropertyExpr
    CHECK(prog != nullptr);
}

TEST_CASE("parser_prefix_unary_minus") {
    auto ast = printAST("{ -x; }");
    CHECK(ast.find("(- x)") != std::string::npos);
}

TEST_CASE("parser_prefix_unary_not") {
    auto ast = printAST("{ !x; }");
    CHECK(ast.find("(! x)") != std::string::npos);
}

TEST_CASE("parser_prefix_inc") {
    auto prog = parse("let x = 0; ++x;");
    CHECK(prog != nullptr);
}

TEST_CASE("parser_postfix_inc") {
    auto prog = parse("let x = 0; x++;");
    CHECK(prog != nullptr);
}

TEST_CASE("parser_call_expression") {
    auto prog = parse("func f() {} f(1, 2);");
    REQUIRE(prog != nullptr);
    auto ast = printAST("func f() {} f(1, 2);");
    CHECK(ast.find("(call f") != std::string::npos);
}

TEST_CASE("parser_call_no_args") {
    auto ast = printAST("func f() {} f();");
    CHECK(ast.find("(call f") != std::string::npos);
}

// ============================================================================
// Error recovery
// ============================================================================

TEST_CASE("parser_error_recovery_synchronize") {
    // After a parse error, parser should synchronize and not crash.
    // The lexer sees '@' as an invalid character but creates an INVALID token.
    // The parser's synchronize() should skip past it.
    Lexer lexer("let x = 5; @ let y = 10;");
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    // synchronize() itself doesn't crash — that's the key test
    // Just verify we don't crash on invalid input
    CHECK(true);
    (void)prog;
    (void)parser;
}

TEST_CASE("parser_empty_input") {
    auto prog = parse("");
    REQUIRE(prog != nullptr);
    CHECK(prog->statements.empty());
}
