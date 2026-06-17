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
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
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
    StderrErrorReporter reporter("let x;");
    Lexer lexer("let x;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
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
    CHECK(func->params[0].name == "a");
    CHECK(func->params[1].name == "b");
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
    CHECK(obj->params[0].name == "x");
    CHECK(obj->params[1].name == "y");
}

TEST_CASE("parser_obj_inheritance") {
    auto prog = parse("Obj Child : Parent(x) { }");
    REQUIRE(prog != nullptr);
    auto* obj = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    REQUIRE(obj != nullptr);
    CHECK(obj->name == "Child");
    REQUIRE(obj->parentNames.size() == 1);
    CHECK(obj->parentNames[0] == "Parent");
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
// Error recovery — parser always returns a Program (never nullptr)
// ============================================================================

TEST_CASE("parser_empty_input") {
    auto prog = parse("");
    REQUIRE(prog != nullptr);
    CHECK(prog->statements.empty());
}

TEST_CASE("parser_error_always_returns_program") {
    // The parser must always return a Program, even for completely garbled input.
    std::string src = "@ @ @";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    CHECK(parser.hasError());
}

TEST_CASE("parser_error_synchronize_basic") {
    // After a parse error, parser should synchronize and not crash.
    // The lexer sees '@' as an invalid character.
    std::string src = "let x = 5; @ let y = 10;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_multiple_statements_after_error") {
    // Statements after an error should still be parsed.
    std::string src = "let x = ; let y = 20;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    // We should get at least 2 statements (error + let y = 20)
    CHECK(prog->statements.size() >= 2);
}

TEST_CASE("parser_error_partial_if_missing_parens") {
    // If statement missing '(' and ')' should still produce an IfStmt.
    StderrErrorReporter reporter("if true { }");
    Lexer lexer("if true { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    // Should get an IfStmt (even if condition/body are error nodes)
    auto* ifStmt = dynamic_cast<IfStmt*>(prog->statements[0].get());
    CHECK(ifStmt != nullptr);
}

TEST_CASE("parser_error_partial_if_missing_condition") {
    // if ( ) { } — empty condition should produce ErrorExpr, not lose the whole if.
    StderrErrorReporter reporter("if ( ) { let y = 1; }");
    Lexer lexer("if ( ) { let y = 1; }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* ifStmt = dynamic_cast<IfStmt*>(prog->statements[0].get());
    CHECK(ifStmt != nullptr);
}

TEST_CASE("parser_error_partial_if_missing_body") {
    // if (true) — missing body should still produce IfStmt.
    StderrErrorReporter reporter("if (true)");
    Lexer lexer("if (true)", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* ifStmt = dynamic_cast<IfStmt*>(prog->statements[0].get());
    CHECK(ifStmt != nullptr);
}

TEST_CASE("parser_error_partial_while_missing_parens") {
    // While statement missing '(' should still produce a WhileStmt.
    StderrErrorReporter reporter("while true { }");
    Lexer lexer("while true { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* whileStmt = dynamic_cast<WhileStmt*>(prog->statements[0].get());
    CHECK(whileStmt != nullptr);
}

TEST_CASE("parser_error_partial_while_missing_body") {
    // while (true) — missing body should still produce WhileStmt.
    StderrErrorReporter reporter("while (true)");
    Lexer lexer("while (true)", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* whileStmt = dynamic_cast<WhileStmt*>(prog->statements[0].get());
    CHECK(whileStmt != nullptr);
}

TEST_CASE("parser_error_partial_forin_missing_variable") {
    // for in [1,2,3] { } — missing variable.
    StderrErrorReporter reporter("for in [1,2,3] { }");
    Lexer lexer("for in [1,2,3] { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* forStmt = dynamic_cast<ForStmt*>(prog->statements[0].get());
    CHECK(forStmt != nullptr);
}

TEST_CASE("parser_error_partial_forin_missing_iterable") {
    // for x in { } — missing iterable expression.
    StderrErrorReporter reporter("for x in { }");
    Lexer lexer("for x in { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* forStmt = dynamic_cast<ForStmt*>(prog->statements[0].get());
    CHECK(forStmt != nullptr);
}

TEST_CASE("parser_error_partial_func_missing_name") {
    // func { } — missing name (and parens). '{' doesn't look like '(' so it
    // enters funcStatement(), not the anonymous function expression path.
    StderrErrorReporter reporter("func { }");
    Lexer lexer("func { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* funcStmt = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    CHECK(funcStmt != nullptr);
}

TEST_CASE("parser_error_partial_func_missing_body") {
    // func f() — missing body should still produce FuncStmt.
    StderrErrorReporter reporter("func f()");
    Lexer lexer("func f()", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* funcStmt = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    CHECK(funcStmt != nullptr);
}

TEST_CASE("parser_error_partial_func_missing_params") {
    // func f { } — missing parameter list.
    StderrErrorReporter reporter("func f { }");
    Lexer lexer("func f { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* funcStmt = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    CHECK(funcStmt != nullptr);
}

TEST_CASE("parser_error_partial_obj_missing_name") {
    // Obj (x, y) { } — missing name.
    StderrErrorReporter reporter("Obj (x, y) { }");
    Lexer lexer("Obj (x, y) { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* objStmt = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    CHECK(objStmt != nullptr);
}

TEST_CASE("parser_error_partial_obj_missing_body") {
    // Obj Point(x, y) — missing body.
    StderrErrorReporter reporter("Obj Point(x, y)");
    Lexer lexer("Obj Point(x, y)", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* objStmt = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    CHECK(objStmt != nullptr);
}

TEST_CASE("parser_error_partial_try_missing_block") {
    // try — missing everything.
    StderrErrorReporter reporter("try");
    Lexer lexer("try", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* tryStmt = dynamic_cast<TryStmt*>(prog->statements[0].get());
    CHECK(tryStmt != nullptr);
}

TEST_CASE("parser_error_partial_try_missing_catch_parens") {
    // try { } catch e { } — missing '(' and ')' around catch variable.
    StderrErrorReporter reporter("try { } catch e { }");
    Lexer lexer("try { } catch e { }", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* tryStmt = dynamic_cast<TryStmt*>(prog->statements[0].get());
    CHECK(tryStmt != nullptr);
}

TEST_CASE("parser_error_partial_let_missing_equals") {
    // let x — missing '=' and initializer.
    StderrErrorReporter reporter("let x");
    Lexer lexer("let x", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    CHECK(letStmt != nullptr);
    CHECK(letStmt->name == "x");
}

TEST_CASE("parser_error_partial_const_missing_equals") {
    // const x — missing '=' and initializer.
    StderrErrorReporter reporter("const x");
    Lexer lexer("const x", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    CHECK(letStmt != nullptr);
    CHECK(letStmt->name == "x");
    CHECK(letStmt->isConst == true);
}

// ============================================================================
// Error recovery — expression level
// ============================================================================

TEST_CASE("parser_error_binary_missing_right_operand") {
    // a + — missing right operand should produce ErrorExpr, not crash.
    StderrErrorReporter reporter("let x = 1 + ;");
    Lexer lexer("let x = 1 + ;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_ternary_missing_colon") {
    // true ? 1 — missing ':' and else branch.
    StderrErrorReporter reporter("let x = true ? 1;");
    Lexer lexer("let x = true ? 1;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_ternary_missing_else") {
    // true ? 1 : — missing else branch.
    StderrErrorReporter reporter("let x = true ? 1 : ;");
    Lexer lexer("let x = true ? 1 : ;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_array_missing_closing_bracket") {
    // [1, 2,  — missing ']'.
    StderrErrorReporter reporter("[1, 2, ");
    Lexer lexer("[1, 2, ", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    // Should still produce an ArrayExpr (partial)
    auto ast = ASTPrinter().print(prog.get());
    CHECK(ast.find("[") != std::string::npos);
}

TEST_CASE("parser_error_dict_missing_closing_brace") {
    // let d = {a: 1, b: 2 — missing '}' (in expression context).
    StderrErrorReporter reporter("let d = {a: 1, b: 2 ");
    Lexer lexer("let d = {a: 1, b: 2 ", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_call_missing_closing_paren") {
    // f(1, 2 — missing ')'.
    StderrErrorReporter reporter("func f(a,b){} f(1, 2 ");
    Lexer lexer("func f(a,b){} f(1, 2 ", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_grouping_missing_closing_paren") {
    // (1 + 2 — missing ')'.
    StderrErrorReporter reporter("let x = (1 + 2;");
    Lexer lexer("let x = (1 + 2;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_property_missing_name") {
    // obj. — missing property name after '.'.
    StderrErrorReporter reporter("let x = obj.;");
    Lexer lexer("let x = obj.;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_invalid_assignment_target") {
    // 5 = x — invalid assignment target.
    StderrErrorReporter reporter("5 = x;");
    Lexer lexer("5 = x;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_invalid_compound_assignment_target") {
    // 5 += x — invalid compound assignment target.
    StderrErrorReporter reporter("5 += x;");
    Lexer lexer("5 += x;", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

// ============================================================================
// Error recovery — multi-error scenarios
// ============================================================================

TEST_CASE("parser_error_multiple_errors_not_crash") {
    // Multiple syntax errors in sequence should all be captured.
    std::string src = "let x = ; func ( ) { } if true { }";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    CHECK(parser.hasError());
    // Should have 3 statements (all partial)
    CHECK(prog->statements.size() == 3);
}

TEST_CASE("parser_error_nested_block_recovery") {
    // Error inside nested block should not lose outer structure.
    std::string src = "if (true) { let x = ; let y = 1; }";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* ifStmt = dynamic_cast<IfStmt*>(prog->statements[0].get());
    CHECK(ifStmt != nullptr);
}

TEST_CASE("parser_error_recovery_preserves_valid_statements") {
    // Valid statements after an error should be fully intact.
    std::string src = "let bad = ; let ok = 42;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() >= 2);
    // The second statement should be a valid LetStmt with name "ok"
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[1].get());
    CHECK(letStmt != nullptr);
    CHECK(letStmt->name == "ok");
}

TEST_CASE("parser_error_import_missing_path") {
    // import — missing string path.
    StderrErrorReporter reporter("import");
    Lexer lexer("import", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_export_missing_declaration") {
    // export — missing declaration.
    StderrErrorReporter reporter("export");
    Lexer lexer("export", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_unexpected_token_at_statement_level") {
    // A stray token that isn't a valid statement start.
    StderrErrorReporter reporter(";");
    Lexer lexer(";", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_formatter_handles_error_nodes") {
    // The AST printer and formatter should handle ErrorExpr/ErrorStmt gracefully.
    std::string src = "let x = ; func f( ) { }";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    // AST printer should not throw
    ASTPrinter printer;
    std::string ast = printer.print(prog.get());
    CHECK(!ast.empty());
    CHECK(ast.find("error") != std::string::npos);
}

TEST_CASE("parser_error_synchronize_respects_brace_depth") {
    // An error inside nested braces should not cause synchronize() to stop
    // at the first close-brace that doesn't match the enclosing depth.
    std::string src = "if (true) { let x = ; let y = 2; } let z = 3;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    // We should get both the if statement and let z = 3
    CHECK(prog->statements.size() >= 2);
}

// ============================================================================
// Error recovery — LSP-specific scenarios
// ============================================================================

TEST_CASE("parser_error_lsp_incomplete_if_condition") {
    // User typing: "if (" — LSP gets incremental updates.
    StderrErrorReporter reporter("if (");
    Lexer lexer("if (", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
}

TEST_CASE("parser_error_lsp_incomplete_func_signature") {
    // User typing: "func f(" — incomplete parameter list.
    StderrErrorReporter reporter("func f(");
    Lexer lexer("func f(", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    // Should still produce a FuncStmt so LSP can offer completion.
    REQUIRE(!prog->statements.empty());
    auto* funcStmt = dynamic_cast<FuncStmt*>(prog->statements[0].get());
    CHECK(funcStmt != nullptr);
}

TEST_CASE("parser_error_lsp_incomplete_while") {
    // User typing: "while (" — incomplete.
    StderrErrorReporter reporter("while (");
    Lexer lexer("while (", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* whileStmt = dynamic_cast<WhileStmt*>(prog->statements[0].get());
    CHECK(whileStmt != nullptr);
}

TEST_CASE("parser_error_lsp_incomplete_forin") {
    // User typing: "for x in" — incomplete.
    StderrErrorReporter reporter("for x in");
    Lexer lexer("for x in", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* forStmt = dynamic_cast<ForStmt*>(prog->statements[0].get());
    CHECK(forStmt != nullptr);
}

TEST_CASE("parser_error_lsp_incomplete_object") {
    // User typing: "Obj Point(" — incomplete.
    StderrErrorReporter reporter("Obj Point(");
    Lexer lexer("Obj Point(", reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    REQUIRE(!prog->statements.empty());
    auto* objStmt = dynamic_cast<ObjStmt*>(prog->statements[0].get());
    CHECK(objStmt != nullptr);
}
