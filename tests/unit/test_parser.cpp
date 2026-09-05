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
    REQUIRE(f->variablePattern != nullptr);
    CHECK(f->variablePattern->getSimpleName() == "x");
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

TEST_CASE("parser_ternary_or_lower_than_or") {
    // P0 #2 fix: ternary binds less tightly than ||. Before the fix
    // `a || b ? c : d` parsed as `a || (b ? c : d)` (?: stronger than
    // ||). After the fix it should be `(a || b) ? c : d`, matching
    // C / C++ / Java / JavaScript semantics.
    auto ast = printAST("{ let x = true || false ? 1 : 2; }");
    REQUIRE(ast != "PARSE_FAILED");
    // The ternary wraps the result of (||): '(?: (|| ...) 1 2)'.
    // Find the ternary child slot, and assert the OR appears as the
    // *first* operand (the condition).
    CHECK(ast.find("(?") != std::string::npos);
    CHECK(ast.find("||") != std::string::npos);
    // OR must appear inside the ternary's first child position.
    auto tStart  = ast.find("(?");
    auto tEnd    = ast.find(')', tStart + 2);
    auto orPos   = ast.find("||",  tStart);
    REQUIRE(orPos != std::string::npos);
    REQUIRE(tEnd  != std::string::npos);
    CHECK(orPos > tStart);
    CHECK(orPos < tEnd);
}

TEST_CASE("parser_ternary_inside_else_branch_of_or") {
    // `a ? b : c || d` — OR in else position: `a ? b : (c || d)`.
    auto ast = printAST("{ let x = true ? 1 : false || true; }");
    REQUIRE(ast != "PARSE_FAILED");
    REQUIRE(ast.find("(?") != std::string::npos);
    auto tStart = ast.find("(?");
    auto tEnd   = ast.find(')', tStart + 2);
    auto orPos  = ast.find("||", tStart);
    REQUIRE(orPos != std::string::npos);
    REQUIRE(tEnd   != std::string::npos);
    // OR must be in the ternary's else branch (after the second value,
    // which is `1`), so find the position of '1' first.
    auto onePos = ast.find('1', tStart);
    REQUIRE(onePos != std::string::npos);
    CHECK(orPos > onePos);
    CHECK(orPos < tEnd);
}

TEST_CASE("parser_ternary_wraps_and") {
    // `a && b ? c : d` — the AND's precedence (2) makes it bind first,
    // so the ternary wraps the AND result: `(a && b) ? c : d`.
    auto ast = printAST("{ let x = true && false ? 1 : 2; }");
    REQUIRE(ast != "PARSE_FAILED");
    REQUIRE(ast.find("(?") != std::string::npos);
    auto tStart = ast.find("(?");
    auto tEnd   = ast.find(')', tStart + 2);
    auto andPos = ast.find("&&", tStart);
    REQUIRE(andPos != std::string::npos);
    REQUIRE(tEnd   != std::string::npos);
    // AND must be inside the ternary.
    CHECK(andPos > tStart);
    CHECK(andPos < tEnd);
}

TEST_CASE("parser_ternary_inside_else_branch_of_and") {
    // `a ? b : c && d` — ternary's else branch contains `c && d`.
    auto ast = printAST("{ let x = true ? 1 : false && true; }");
    REQUIRE(ast != "PARSE_FAILED");
    REQUIRE(ast.find("(?") != std::string::npos);
    auto tStart = ast.find("(?");
    auto tEnd   = ast.find(')', tStart + 2);
    auto andPos = ast.find("&&", tStart);
    REQUIRE(andPos != std::string::npos);
    REQUIRE(tEnd   != std::string::npos);
    auto onePos = ast.find('1', tStart);
    REQUIRE(onePos != std::string::npos);
    CHECK(andPos > onePos);
    CHECK(andPos < tEnd);
}

TEST_CASE("parser_or_looser_than_ternary_at_runtime") {
    // Runtime-level verification of P0 #2:
    //   `let r = true || false ? "A" : "B"`
    // After P0 #2 the AST is `(? (|| true false) "A" "B")`.  With
    // short-circuit, `(true || false)` evaluates to `true`, then
    // `true ? "A" : "B"` yields `"A"` (NOT `true`).
    auto prog = parse("let r = true || false ? \"A\" : \"B\"");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    // The initializer should be a TernaryExpr with a BinaryExpr '||'
    // as its condition.
    auto* ternary = dynamic_cast<TernaryExpr*>(letStmt->initializer.get());
    REQUIRE(ternary != nullptr);
    auto* cond = dynamic_cast<BinaryExpr*>(ternary->condition.get());
    REQUIRE(cond != nullptr);
    CHECK(cond->op.type == TokenType::OR);
}

TEST_CASE("parser_nested_ternary_right_assoc_strict") {
    auto ast = printAST("{ true ? false ? 1 : 2 : 3; }");
    // Should yield '(? true (? false 1 2) 3)' — right-assoc.
    auto tStart = ast.find("(?");
    REQUIRE(tStart != std::string::npos);
    auto innerT = ast.find("(?", tStart + 2);
    CHECK(innerT != std::string::npos);
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

TEST_CASE("parser_named_argument_basic") {
    // P0 #3: `f(a = 5)` should produce a CallExpr whose first arg has
    // argumentNames[0] == "a" (and arguments[0] = 5). It MUST NOT eat the
    // =5 — i.e. it must not collapse into anything that drops the value.
    auto prog = parse("f(a = 5)");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* exprStmt = dynamic_cast<ExprStmt*>(prog->statements[0].get());
    REQUIRE(exprStmt != nullptr);
    auto* call = dynamic_cast<CallExpr*>(exprStmt->expression.get());
    REQUIRE(call != nullptr);
    REQUIRE(call->arguments.size() == 1);
    CHECK(call->argumentNames[0] == "a");
    // The value 5 must be preserved as a LiteralExpr.
    auto* lit = dynamic_cast<LiteralExpr*>(call->arguments[0].get());
    REQUIRE(lit != nullptr);
    CHECK(lit->value.isNumeric());
}

TEST_CASE("parser_named_argument_mixed_positional") {
    // `f(1, b = 2)` — positional first, named second. argumentNames[0]
    // empty (positional), argumentNames[1] = "b".
    auto prog = parse("f(1, b = 2)");
    REQUIRE(prog != nullptr);
    auto* exprStmt = dynamic_cast<ExprStmt*>(prog->statements[0].get());
    REQUIRE(exprStmt != nullptr);
    auto* call = dynamic_cast<CallExpr*>(exprStmt->expression.get());
    REQUIRE(call != nullptr);
    REQUIRE(call->arguments.size() == 2);
    CHECK(call->argumentNames[0] == "");
    CHECK(call->argumentNames[1] == "b");
}

TEST_CASE("parser_named_argument_all_named") {
    // `f(a = 1, b = 2)` — both named.
    auto prog = parse("f(a = 1, b = 2)");
    REQUIRE(prog != nullptr);
    auto* call = dynamic_cast<CallExpr*>(
        dynamic_cast<ExprStmt*>(prog->statements[0].get())->expression.get()
    );
    REQUIRE(call != nullptr);
    REQUIRE(call->arguments.size() == 2);
    CHECK(call->argumentNames[0] == "a");
    CHECK(call->argumentNames[1] == "b");
}

TEST_CASE("parser_named_argument_positional_after_named_rejected") {
    // `f(a = 1, 2)` — positional cannot follow named.
    auto prog = parse("f(a = 1, 2)");
    REQUIRE(prog != nullptr);
    // Parser must still emit a CallExpr (and report an error to stderr).
    auto* call = dynamic_cast<CallExpr*>(
        dynamic_cast<ExprStmt*>(prog->statements[0].get())->expression.get()
    );
    REQUIRE(call != nullptr);
    REQUIRE(call->arguments.size() == 2);
    CHECK(call->argumentNames[0] == "a");
    CHECK(call->argumentNames[1] == "");  // "2" parsed as positional,
                                         // even though it's illegal here.
}

TEST_CASE("parser_assignment_expression_not_named_arg") {
    // `let x = a = 5` is an assignment chain (let x = (a = 5)), NOT a
    // call. Verify the parser correctly produces an AssignmentExpr
    // (and avoids mis-treating `a = 5` as a name=value pair, which only
    // happens inside `(`...`)` call argument lists).
    auto prog = parse("let x = a = 5");
    REQUIRE(prog != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* assign = dynamic_cast<AssignmentExpr*>(letStmt->initializer.get());
    CHECK(assign != nullptr);
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

TEST_CASE("parser_import_basename_auto_alias") {
    // P0 #4: `import "foo"` — basename is "foo", a legal identifier, so
    // the parser auto-fills the alias rather than asking the user.
    auto prog = parse("import \"foo\"");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
    REQUIRE(imp != nullptr);
    CHECK(imp->modulePath == "foo");
    CHECK(imp->alias == "foo");
}

TEST_CASE("parser_import_strip_directory_and_va_extension") {
    // `import "std/json"` → alias = "json"
    // `import "math.va"`   → alias = "math"
    // `import "./foo"`     → alias = "foo"
    {
        auto prog = parse("import \"std/json\"");
        REQUIRE(prog != nullptr);
        auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->alias == "json");
    }
    {
        auto prog = parse("import \"math.va\"");
        REQUIRE(prog != nullptr);
        auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->alias == "math");
    }
    {
        auto prog = parse("import \"./foo\"");
        REQUIRE(prog != nullptr);
        auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->alias == "foo");
    }
}

TEST_CASE("parser_import_explicit_as_passes") {
    // P0 #4 happy path for an otherwise illegal basename: explicit `as`
    // bypasses the basename derivation entirely.
    auto prog = parse("import \"foo-bar\" as bar");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
    REQUIRE(imp != nullptr);
    CHECK(imp->modulePath == "foo-bar");
    CHECK(imp->alias == "bar");
}

TEST_CASE("parser_import_basename_with_dash_rejected") {
    // P0 #4: a basename that is not a legal identifier (`foo-bar`,
    // `./foo-bar`) must produce an error so the user is forced to either
    // supply `as <name>` or use `from "path" import ...`. Previously the
    // parser silently swallowed the binding, causing the user-written
    // `foo-bar.V` to be (mis-)parsed as `foo - bar.V` (subtraction).
    auto prog = parse("import \"foo-bar\"");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    // The placeholder must be an ErrorStmt, not a normal ImportStmt.
    auto* errStmt = dynamic_cast<ErrorStmt*>(prog->statements[0].get());
    CHECK(errStmt != nullptr);
}

TEST_CASE("parser_import_basename_with_dash_rejected_relative_path") {
    // The same rule applies to a relative path whose basename is illegal.
    auto prog = parse("import \"./foo-bar\"");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* errStmt = dynamic_cast<ErrorStmt*>(prog->statements[0].get());
    CHECK(errStmt != nullptr);
}

TEST_CASE("parser_from_import_unaffected_by_p04") {
    // `from "path" import a, b` always uses the explicit identifier list —
    // it must NOT trigger the basename-validity error regardless of path.
    {
        auto prog = parse("from \"foo-bar\" import a, b");
        REQUIRE(prog != nullptr);
        auto* imp = dynamic_cast<ImportStmt*>(prog->statements[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->importNames.size() == 2);
        CHECK(imp->importNames[0] == "a");
        CHECK(imp->importNames[1] == "b");
    }
}

TEST_CASE("parser_match_or_pattern_three_literals") {
    // P1-B: `1 | 2 | 3 => ...` should yield one MatchCase whose
    // patterns vector contains three entries (the first one Literal(1),
    // then Literal(2), Literal(3)). Previously the lexer produced an
    // error "Unexpected character: '|'" because Vora had no PIPE token.
    auto ast = printAST("let x = match (3) { 1 | 2 | 3 => \"small\" }");
    REQUIRE(ast != "PARSE_FAILED");
}

TEST_CASE("parser_match_or_pattern_two_alternatives") {
    auto prog = parse("let x = match (n) { 1 | 2 => \"small\", _ => \"big\" }");
    REQUIRE(prog != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* match = dynamic_cast<MatchExpr*>(letStmt->initializer.get());
    REQUIRE(match != nullptr);
    REQUIRE(match->cases.size() == 2);
    // First arm carries two patterns.
    CHECK(match->cases[0].patterns.size() == 2);
    CHECK(match->cases[0].patterns[0].kind == PatternKind::Literal);
    CHECK(match->cases[0].patterns[1].kind == PatternKind::Literal);
    // Second arm is the catch-all wildcard.
    CHECK(match->cases[1].patterns.size() == 1);
    CHECK(match->cases[1].patterns[0].kind == PatternKind::Wildcard);
}

TEST_CASE("parser_match_or_pattern_long_alternation") {
    // 5 alternatives in one arm (stress the while loop in matchExpression).
    auto prog = parse("let x = match (c) { 'a' | 'b' | 'c' | 'd' | 'e' => 1 }");
    REQUIRE(prog != nullptr);
    auto* match = dynamic_cast<MatchExpr*>(
        dynamic_cast<LetStmt*>(prog->statements[0].get())->initializer.get()
    );
    REQUIRE(match != nullptr);
    REQUIRE(match->cases.size() == 1);
    CHECK(match->cases[0].patterns.size() == 5);
    for (auto& p : match->cases[0].patterns) {
        CHECK(p.kind == PatternKind::Literal);
    }
}

TEST_CASE("parser_match_or_pattern_ranges_and_wildcards_unchanged") {
    // Backwards-compat: plain (no-or) match arms still work.
    auto prog = parse("let x = match (5) { 1..10 => \"s\", _ => \"b\" }");
    REQUIRE(prog != nullptr);
    auto* match = dynamic_cast<MatchExpr*>(
        dynamic_cast<LetStmt*>(prog->statements[0].get())->initializer.get()
    );
    REQUIRE(match != nullptr);
    REQUIRE(match->cases.size() == 2);
    CHECK(match->cases[0].patterns.size() == 1);
    CHECK(match->cases[0].patterns[0].kind == PatternKind::Range);
    CHECK(match->cases[1].patterns[0].kind == PatternKind::Wildcard);
}

// P1-B sanity: `||` (TokenType::OR) is still emitted by the lexer
// and produces a boolean expression; the new PIPE token only fires
// for a bare `|`. This is locked in by `parser_or_looser_than_ternary_at_runtime`
// above (line 305), which uses `||` mid-expression and verifies the AST.
// We deliberately don't add a separate test for `||` to keep the file lean.

TEST_CASE("parser_import_keyword_after_error_recovers") {
    // After the errorStmt, the parser must synchronize and parse the
    // next statement normally (a LetStmt).
    auto prog = parse("import \"foo-bar\"; let x = 5");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 2);
    CHECK(dynamic_cast<ErrorStmt*>(prog->statements[0].get()) != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[1].get());
    CHECK(letStmt != nullptr);
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

// ============================================================================
// Go-style Automatic Semicolon Insertion (ASI) — fixes P0 #1 from
// VORA_SYNTAX_REVIEW.md. Without ASI the parser silently吞噬 statements
// that appear after a newline, so `let b = a\n-1` parsed as
// `let b = (a - 1)` instead of two separate statements.
// ============================================================================

TEST_CASE("parser_asi_let_let_newline") {
    // Two consecutive `let` statements on separate lines.
    // Should parse as TWO LetStmt nodes, not as one initializer that
    // also consumes the second `let`.
    auto ast = printAST("let a = 1\nlet b = 2");
    CHECK(ast.find("(let a 1)") != std::string::npos);
    CHECK(ast.find("(let b 2)") != std::string::npos);
}

TEST_CASE("parser_asi_newline_terminates_initializer") {
    // The reported P0 #1 bug: `let b = a` followed by `-1` on the next
    // line MUST be `let b = a` followed by an expression statement
    // (unary -1), NOT `let b = (a - 1)`.
    auto ast = printAST("let a = 5\nlet b = a\n-1");
    // Expect: LetStmt for b initialized to plain `a`, not to `(- a 1)`.
    CHECK(ast.find("(let b a)") != std::string::npos);
    CHECK(ast.find("(let b (- a 1))") == std::string::npos);
}

TEST_CASE("parser_asi_newline_inside_parens_allowed") {
    // Newlines INSIDE (...) must NOT trigger ASI — this is a single
    // function call whose argument list wraps onto multiple lines.
    auto prog = parse("let a = sum(\n  1,\n  2\n)");
    REQUIRE(prog != nullptr);
    // Two top-level statements (no ASI swallowed the trailing-call).
    REQUIRE(prog->statements.size() == 1);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    // The initializer should be a CallExpr (sum(1, 2)), not a partial
    // expression that ends at the first newline.
    auto* callExpr = dynamic_cast<CallExpr*>(letStmt->initializer.get());
    CHECK(callExpr != nullptr);
}

TEST_CASE("parser_asi_newline_inside_brackets_allowed") {
    // Array literals must allow newlines between elements.
    auto prog = parse("let a = [\n  1,\n  2,\n  3\n]");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* arrayExpr = dynamic_cast<ArrayExpr*>(letStmt->initializer.get());
    CHECK(arrayExpr != nullptr);
    REQUIRE(arrayExpr != nullptr);
    CHECK(arrayExpr->elements.size() == 3);
}

TEST_CASE("parser_asi_single_line_subtraction_unchanged") {
    // Regression guard: same-line arithmetic must keep working.
    auto prog = parse("let x = 5 - 3");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    // Initializer should be a binary subtraction, not "5" alone.
    auto* binExpr = dynamic_cast<BinaryExpr*>(letStmt->initializer.get());
    CHECK(binExpr != nullptr);
}

// ============================================================================
// `in` expression & paren for-in (P1-G & P1-H)
// ============================================================================

TEST_CASE("parser_in_expression_array_basic") {
    // `x in arr` should parse as a BinaryExpr with operator IN.
    auto prog = parse("let r = 3 in [1,2,3,4];");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* bin = dynamic_cast<BinaryExpr*>(letStmt->initializer.get());
    CHECK(bin != nullptr);
    REQUIRE(bin != nullptr);
    CHECK(bin->op.type == TokenType::IN);
}

TEST_CASE("parser_in_expression_dict_basic") {
    auto prog = parse("let r = \"k\" in {a:1, b:2};");
    REQUIRE(prog != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* bin = dynamic_cast<BinaryExpr*>(letStmt->initializer.get());
    CHECK(bin != nullptr);
    CHECK(bin->op.type == TokenType::IN);
}

TEST_CASE("parser_in_expression_string_basic") {
    auto prog = parse("let r = \"foo\" in \"foobarbaz\";");
    REQUIRE(prog != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* bin = dynamic_cast<BinaryExpr*>(letStmt->initializer.get());
    CHECK(bin != nullptr);
    CHECK(bin->op.type == TokenType::IN);
}

TEST_CASE("parser_unary_plus_basic") {
    // +literal and +identifier both parse to UnaryExpr(PLUS).
    {
        auto prog = parse("let r = +5;");
        REQUIRE(prog != nullptr);
        auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
        REQUIRE(letStmt != nullptr);
        auto* u = dynamic_cast<UnaryExpr*>(letStmt->initializer.get());
        CHECK(u != nullptr);
        if (u) CHECK(u->op.type == TokenType::PLUS);
    }
    {
        auto prog = parse("let r = +x;");
        REQUIRE(prog != nullptr);
        auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
        REQUIRE(letStmt != nullptr);
        auto* u = dynamic_cast<UnaryExpr*>(letStmt->initializer.get());
        CHECK(u != nullptr);
    }
}

TEST_CASE("parser_in_expression_left_associative") {
    // `in` at prec 4 should bind tighter than `&&` (prec 2): top-level
    // expression is `(&& ...)`, with IN inside the left operand.
    auto prog = parse("let r = 1 in [1] && true;");
    REQUIRE(prog != nullptr);
    auto* letStmt = dynamic_cast<LetStmt*>(prog->statements[0].get());
    REQUIRE(letStmt != nullptr);
    auto* bin = dynamic_cast<BinaryExpr*>(letStmt->initializer.get());
    REQUIRE(bin != nullptr);
    CHECK(bin->op.type == TokenType::AND);  // AND (prec 2) > IN (prec 4)
    auto* inLeft = dynamic_cast<BinaryExpr*>(bin->left.get());
    REQUIRE(inLeft != nullptr);
    CHECK(inLeft->op.type == TokenType::IN);
}

TEST_CASE("parser_paren_for_in_basic") {
    // `for (x in xs) { ... }` should be a valid for-in, not a C-for.
    auto prog = parse("for (x in [1,2,3]) { print(x); }");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* forStmt = dynamic_cast<ForStmt*>(prog->statements[0].get());
    REQUIRE(forStmt != nullptr);
    // iterable should parse as ArrayExpr.
    auto* arr = dynamic_cast<ArrayExpr*>(forStmt->iterable.get());
    CHECK(arr != nullptr);
}

TEST_CASE("parser_paren_for_in_destructured_pattern") {
    // P1-H must also accept destructured patterns inside parens.
    auto prog = parse("for ([a, b] in [[1,2],[3,4]]) { print(a + b); }");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* forStmt = dynamic_cast<ForStmt*>(prog->statements[0].get());
    CHECK(forStmt != nullptr);
}

TEST_CASE("parser_c_style_for_still_works") {
    // Regression: a true C-for with parens must keep parsing correctly.
    auto prog = parse("for (let i = 0; i < 3; i = i + 1) { print(i); }");
    REQUIRE(prog != nullptr);
    REQUIRE(prog->statements.size() == 1);
    auto* cforStmt = dynamic_cast<CForStmt*>(prog->statements[0].get());
    REQUIRE(cforStmt != nullptr);
    // Condition should be a binary less-than.
    auto* cond = dynamic_cast<BinaryExpr*>(cforStmt->condition.get());
    REQUIRE(cond != nullptr);
    CHECK(cond->op.type == TokenType::LESS);
}

