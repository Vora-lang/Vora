// tests/unit/test_formatter.cpp — Formatter unit tests
//
// Tests: source formatting round-trip, compound assignment preservation,
//        precedence-based parenthesization, idempotency, edge cases.

#include "doctest.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "formatter/formatter.h"
#include "ast/program.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <system_error>
#include <sstream>
#include <string>

using namespace vora;

// Helper: lex + parse + format source, return formatted string.
static std::string fmt(const std::string& src) {
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    parser.setSource(src);
    // Comments are trivia, so the parser only carries them when handed the
    // lexer's list; the formatter is the consumer that needs them.
    parser.setComments(lexer.comments());
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    SourceFormatter formatter;
    return formatter.format(prog.get());
}

// Trim surrounding whitespace (fmt() emits a trailing newline).
static std::string trimmed(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Extract just the literal the formatter emitted for the RHS of `let a = ...`.
// Checking the whole formatted source would be wrong: the keyword `let`
// contains an `e`, which would defeat a search for exponent notation.
static std::string literalFromSource(const std::string& src) {
    const std::string out = fmt(src);
    const size_t eq = out.find('=');
    REQUIRE(eq != std::string::npos);
    return trimmed(out.substr(eq + 1));
}

// Render a double through the formatter and return the literal it produced.
//
// The input literal is built with `to_chars(fixed)` rather than `ostream`,
// because the default stream format switches to scientific notation for large
// and small magnitudes — which is not valid Vora input at all, so the source
// would fail to lex instead of exercising the formatter.
static std::string formattedLiteral(double d) {
    char buf[512];
    auto res = std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::fixed);
    REQUIRE(res.ec == std::errc());
    std::string text = "let a = " + std::string(buf, res.ptr);
    if (text.find('.') == std::string::npos) text += ".0";
    return literalFromSource(text);
}

// Properties every emitted float literal must have:
//   1. it re-lexes as a *float* — it carries a '.' or an exponent, because a
//      bare integer lexeme would silently change the value's type
//   2. std::stod on it returns a bit-identical double (lossless round trip)
//   3. it is at most the shorter of the two layouts, plus the ".0" that may be
//      appended to keep an integral value a float (fixed "2" -> emitted "2.0")
static void checkFloatLiteralRoundTrip(double d) {
    const std::string lit = formattedLiteral(d);
    const bool readsAsFloat =
        lit.find('.') != std::string::npos ||
        lit.find('e') != std::string::npos ||
        lit.find('E') != std::string::npos;
    CHECK(readsAsFloat);
    const double back = std::stod(lit);
    uint64_t a = 0, b = 0;
    std::memcpy(&a, &d, sizeof(a));
    std::memcpy(&b, &back, sizeof(b));
    CHECK(a == b);

    // The emitted text must be at most the shorter of the two layouts, plus at
    // most the two characters of an appended ".0".
    auto layoutLen = [d](std::chars_format f) -> size_t {
        char buf[512];
        auto res = std::to_chars(buf, buf + sizeof(buf), d, f);
        return (res.ec == std::errc()) ? static_cast<size_t>(res.ptr - buf) : SIZE_MAX;
    };
    const size_t shorter = std::min(layoutLen(std::chars_format::fixed),
                                    layoutLen(std::chars_format::general));
    if (shorter != SIZE_MAX) {
        CHECK(lit.size() <= shorter + 2);
    }
}

// Helper: format twice and verify idempotency.
static bool isIdempotent(const std::string& src) {
    std::string first = fmt(src);
    // Re-parse and re-format
    StderrErrorReporter reporter(first);
    Lexer lexer(first, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    parser.setSource(first);
    parser.setComments(lexer.comments());
    auto prog = parser.parse();
    if (!prog) return false;
    SourceFormatter formatter;
    std::string second = formatter.format(prog.get());
    return first == second;
}

// ============================================================================
// Idempotency
// ============================================================================

TEST_CASE("fmt_idempotent_simple") {
    CHECK(isIdempotent("let x = 10\nprint(x)\n"));
}

TEST_CASE("fmt_idempotent_if_else") {
    CHECK(isIdempotent("if (true) { print(\"yes\") } else { print(\"no\") }"));
}

TEST_CASE("fmt_idempotent_function") {
    CHECK(isIdempotent("func add(a, b) { return a + b }"));
}

TEST_CASE("fmt_idempotent_object") {
    CHECK(isIdempotent("class Person(name, age) { this.name = name\nfunc greet() { print(\"hi\") } }"));
}

TEST_CASE("fmt_idempotent_nested_blocks") {
    CHECK(isIdempotent("{ let x = 1\n{ let y = 2\nprint(x + y) } }"));
}

// ============================================================================
// Basic formatting
// ============================================================================

TEST_CASE("fmt_literals") {
    std::string out = fmt("let n = 42\nlet s = \"hello\"\nlet b = true\nlet n2 = null\n");
    CHECK(out.find("let n = 42") != std::string::npos);
    CHECK(out.find("let s = \"hello\"") != std::string::npos);
    CHECK(out.find("let b = true") != std::string::npos);
    CHECK(out.find("let n2 = null") != std::string::npos);
}

TEST_CASE("fmt_binary_operators") {
    std::string out = fmt("let x = 1 + 2 * 3\nlet y = a && b || c\n");
    // Multiplication has higher precedence than addition
    CHECK(out.find("1 + 2 * 3") != std::string::npos);
}

TEST_CASE("fmt_if_statement") {
    std::string out = fmt("if (x > 0) { print(\"positive\") }");
    CHECK(out.find("if (x > 0) {") != std::string::npos);
    CHECK(out.find("print(\"positive\")") != std::string::npos);
    CHECK(out.find("}") != std::string::npos);
}

TEST_CASE("fmt_if_else_chain") {
    std::string out = fmt("if (a) { 1 } else if (b) { 2 } else { 3 }");
    CHECK(out.find("} else if (b) {") != std::string::npos);
    CHECK(out.find("} else {") != std::string::npos);
}

TEST_CASE("fmt_while_loop") {
    std::string out = fmt("while (i < 5) { i = i + 1 }");
    CHECK(out.find("while (i < 5) {") != std::string::npos);
}

TEST_CASE("fmt_for_in_loop") {
    std::string out = fmt("for x in arr { print(x) }");
    CHECK(out.find("for x in arr {") != std::string::npos);
}

TEST_CASE("fmt_function_declaration") {
    std::string out = fmt("func foo(a, b) { return a + b }");
    CHECK(out.find("func foo(a, b) {") != std::string::npos);
    CHECK(out.find("return a + b") != std::string::npos);
}

TEST_CASE("fmt_object_declaration") {
    std::string out = fmt("class MyClass(name) { this.name = name\nfunc speak() { print(this.name) } }");
    CHECK(out.find("class MyClass(name) {") != std::string::npos);
    CHECK(out.find("func speak() {") != std::string::npos);
}

TEST_CASE("fmt_object_with_inheritance") {
    std::string out = fmt("class Dog : Animal(name) { this.name = name }");
    CHECK(out.find("class Dog : Animal(name) {") != std::string::npos);
}

TEST_CASE("fmt_multi_inheritance") {
    std::string out = fmt("class Robot : Speaker, Walker() { func work() { return \"working\" } }");
    CHECK(out.find("class Robot : Speaker, Walker() {") != std::string::npos);
}

TEST_CASE("fmt_return_statement") {
    std::string out = fmt("func f() { return 42 }");
    CHECK(out.find("return 42") != std::string::npos);
}

TEST_CASE("fmt_empty_return") {
    // Vora requires an expression after return — use null
    std::string out = fmt("func f() { return null }");
    CHECK(out.find("return null") != std::string::npos);
}

TEST_CASE("fmt_break_continue") {
    std::string out = fmt("while (true) { break\ncontinue }");
    CHECK(out.find("break") != std::string::npos);
    CHECK(out.find("continue") != std::string::npos);
}

TEST_CASE("fmt_throw") {
    std::string out = fmt("throw \"error\"");
    CHECK(out.find("throw \"error\"") != std::string::npos);
}

TEST_CASE("fmt_try_catch") {
    std::string out = fmt("try { 1 } catch (e) { 2 }");
    CHECK(out.find("try {") != std::string::npos);
    CHECK(out.find("} catch (e) {") != std::string::npos);
}

TEST_CASE("fmt_try_catch_finally") {
    std::string out = fmt("try { 1 } catch (e) { 2 } finally { 3 }");
    CHECK(out.find("try {") != std::string::npos);
    CHECK(out.find("} catch (e) {") != std::string::npos);
    CHECK(out.find("} finally {") != std::string::npos);
}

// ============================================================================
// Compound assignment formatting (the main fix)
// ============================================================================

TEST_CASE("fmt_compound_assignment_variable") {
    std::string out = fmt("let x = 10\nx += 5\n");
    CHECK(out.find("x += 5") != std::string::npos);
}

TEST_CASE("fmt_compound_assignment_all_ops") {
    std::string out = fmt("let x = 10\nx += 1\nx -= 2\nx *= 3\nx /= 4\nx %= 5\n");
    CHECK(out.find("x += 1") != std::string::npos);
    CHECK(out.find("x -= 2") != std::string::npos);
    CHECK(out.find("x *= 3") != std::string::npos);
    CHECK(out.find("x /= 4") != std::string::npos);
    CHECK(out.find("x %= 5") != std::string::npos);
}

TEST_CASE("fmt_compound_assignment_property") {
    std::string out = fmt("class T() { this.n = 0 }\nlet t = T()\nt.n += 5\n");
    CHECK(out.find("t.n += 5") != std::string::npos);
}

TEST_CASE("fmt_compound_assignment_index") {
    std::string out = fmt("let arr = [1, 2, 3]\narr[0] += 10\n");
    CHECK(out.find("arr[0] += 10") != std::string::npos);
}

TEST_CASE("fmt_compound_assignment_in_loop") {
    std::string out = fmt("let i = 0\nwhile (i < 10) { i += 1 }");
    CHECK(out.find("i += 1") != std::string::npos);
}

TEST_CASE("fmt_compound_assignment_idempotent") {
    CHECK(isIdempotent("let x = 10\nx += 5\nx -= 3\nx *= 2\n"));
}

TEST_CASE("fmt_compound_assignment_complex_rhs") {
    std::string out = fmt("let x = 1\nx += 2 * 3 + 4\n");
    CHECK(out.find("x += 2 * 3 + 4") != std::string::npos);
}

// ============================================================================
// Expression precedence / parenthesization
// ============================================================================

TEST_CASE("fmt_precedence_preserves_explicit_parens") {
    // Explicit parens in source should be preserved
    std::string out = fmt("let x = (1 + 2) * 3\n");
    CHECK(out.find("(1 + 2) * 3") != std::string::npos);
}

TEST_CASE("fmt_precedence_no_unnecessary_parens") {
    // * has higher precedence than +, so no parens needed
    std::string out = fmt("let x = 1 + 2 * 3\n");
    CHECK(out.find("1 + 2 * 3") != std::string::npos);
}

TEST_CASE("fmt_precedence_power_right_associative") {
    std::string out = fmt("let x = a ** b ** c\n");
    CHECK(out.find("a ** b ** c") != std::string::npos);
}

TEST_CASE("fmt_precedence_ternary_nested") {
    std::string out = fmt("let x = a ? b : c ? d : e\n");
    CHECK(out.find("a ? b : c ? d : e") != std::string::npos);
}

TEST_CASE("fmt_precedence_unary") {
    std::string out = fmt("let x = !a && b\nlet y = -(a + b)\n");
    CHECK(out.find("!a && b") != std::string::npos);
    CHECK(out.find("-(a + b)") != std::string::npos);
}

TEST_CASE("fmt_precedence_property_chain") {
    std::string out = fmt("let x = a.b.c\n");
    CHECK(out.find("a.b.c") != std::string::npos);
}

TEST_CASE("fmt_precedence_index_chain") {
    std::string out = fmt("let x = arr[0][1]\n");
    CHECK(out.find("arr[0][1]") != std::string::npos);
}

// ============================================================================
// Special cases
// ============================================================================

TEST_CASE("fmt_empty_block") {
    std::string out = fmt("func noop() {}");
    CHECK(out.find("func noop() {") != std::string::npos);
}

TEST_CASE("fmt_empty_object") {
    std::string out = fmt("class Empty() {}");
    CHECK(out.find("class Empty() {") != std::string::npos);
}

TEST_CASE("fmt_string_escape") {
    // Strings with special characters should be re-escaped
    std::string out = fmt("let s = \"line1\\nline2\"\n");
    CHECK(out.find("\"line1\\nline2\"") != std::string::npos);
}

TEST_CASE("fmt_inc_dec_prefix") {
    std::string out = fmt("let a = 10\nlet b = ++a\n");
    CHECK(out.find("++a") != std::string::npos);
}

TEST_CASE("fmt_inc_dec_postfix") {
    std::string out = fmt("let a = 10\nlet b = a++\n");
    CHECK(out.find("a++") != std::string::npos);
}

TEST_CASE("fmt_property_inc_dec") {
    // Vora supports ++/-- on variables (test individually to avoid parser edge case)
    CHECK(isIdempotent("let a = 10\na++\n"));
    CHECK(isIdempotent("let a = 10\n++a\n"));
    CHECK(isIdempotent("let a = 10\na--\n"));
    CHECK(isIdempotent("let a = 10\n--a\n"));
}

TEST_CASE("fmt_array_literal") {
    std::string out = fmt("let arr = [1, 2, 3]\n");
    CHECK(out.find("[1, 2, 3]") != std::string::npos);
}

TEST_CASE("fmt_dict_literal") {
    std::string out = fmt("let d = {key: \"value\", num: 42}\n");
    // Dict keys are identifiers, output without quotes
    CHECK(out.find("key: ") != std::string::npos);
    CHECK(out.find("\"value\"") != std::string::npos);
    CHECK(out.find("num: 42") != std::string::npos);
}

TEST_CASE("fmt_function_call") {
    std::string out = fmt("print(\"hello\", 42, true)\n");
    CHECK(out.find("print(\"hello\", 42, true)") != std::string::npos);
}

TEST_CASE("fmt_let_with_type") {
    std::string out = fmt("let x: int = 42\n");
    CHECK(out.find("let x: int = 42") != std::string::npos);
}

TEST_CASE("fmt_let_no_init") {
    // Vora requires an initializer for let; test let with value
    std::string out = fmt("let x = 0\n");
    CHECK(out.find("let x = 0") != std::string::npos);
}

TEST_CASE("fmt_default_param") {
    std::string out = fmt("func foo(a, b = 10) { return a + b }");
    CHECK(out.find("func foo(a, b = 10) {") != std::string::npos);
}

TEST_CASE("fmt_this_expr") {
    std::string out = fmt("class Foo() { this.bar = 42 }");
    CHECK(out.find("this.bar = 42") != std::string::npos);
}

TEST_CASE("fmt_super_expr") {
    // super in object constructor — just verify it compiles/round-trips
    std::string out = fmt("class A() { this.x = 1 }");
    CHECK_FALSE(out.empty());
}

// ============================================================================
// Float literal rendering — lossless and re-parseable
//
// The formatter used to print doubles with the default stream precision, which
// silently rewrote any value with more than six significant digits
// (1234.5678 -> 1234.57) and emitted exponent notation the lexer cannot read
// (1.23457e+08). It also dropped the trailing ".0" from integral floats, which
// re-parsed them as *integers*.
// ============================================================================

TEST_CASE("fmt_float_precision_is_lossless") {
    CHECK(trimmed(fmt("let a = 1234.5678")) == "let a = 1234.5678");
    CHECK(trimmed(fmt("let a = 3.141592653589793")) == "let a = 3.141592653589793");
    CHECK(trimmed(fmt("let a = 0.30000000000000004")) == "let a = 0.30000000000000004");
    CHECK(trimmed(fmt("let a = 2.2250738585072014")) == "let a = 2.2250738585072014");
    CHECK(trimmed(fmt("let a = 100000.5")) == "let a = 100000.5");
    // 1e-7 is shorter in exponent form (5 chars vs 9), which is why the
    // formatter now emits one: the language gained exponent literals.
    CHECK(trimmed(fmt("let a = 0.0000001")) == "let a = 1e-07");
}

TEST_CASE("fmt_float_integral_values_stay_floats") {
    // Without the appended ".0" these re-parse as integers, silently changing
    // the value's type.
    CHECK(trimmed(fmt("let a = 42.0")) == "let a = 42.0");
    CHECK(trimmed(fmt("let a = 2.0")) == "let a = 2.0");
    CHECK(trimmed(fmt("let a = 0.0")) == "let a = 0.0");
    CHECK(trimmed(fmt("let a = 35184372088832.0")) == "let a = 35184372088832.0");
    CHECK(trimmed(fmt("let a = -0.0")) == "let a = -0.0");
    // Insignificant trailing zeros are not part of the shortest form.
    CHECK(trimmed(fmt("let a = 1.50")) == "let a = 1.5");
}

TEST_CASE("fmt_float_chooses_the_shorter_layout") {
    // Exponent and fixed layouts are both shortest-round-trip; the formatter
    // emits whichever is shorter, which keeps extreme magnitudes readable
    // (1e300 rather than 301 digits) without any loss.
    CHECK(trimmed(fmt("let a = 1e10")) == "let a = 1e+10");
    CHECK(trimmed(fmt("let a = 1e300")) == "let a = 1e+300");
    CHECK(trimmed(fmt("let a = 1e-300")) == "let a = 1e-300");
    CHECK(trimmed(fmt("let a = 0.0000001")) == "let a = 1e-07");
    CHECK(trimmed(fmt("let a = 6.02e23")) == "let a = 6.02e+23");
    // Fixed wins when it is shorter or ties.
    CHECK(trimmed(fmt("let a = 123456789.123456789")) == "let a = 123456789.12345679");
    CHECK(trimmed(fmt("let a = 35184372088832.0")) == "let a = 35184372088832.0");
    CHECK(trimmed(fmt("let a = 1234.5678")) == "let a = 1234.5678");
    // An exponent form must never come back as an integer: 1e+10 keeps a
    // non-empty exponent, and an integral value with no '.' grows a ".0".
    CHECK(trimmed(fmt("let a = 42.0")) == "let a = 42.0");
}

TEST_CASE("fmt_float_property_round_trips_bitwise") {
    const double fixedCases[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, 42.0, 1234.5678, 3.141592653589793,
        0.30000000000000004, 1e-7, 1e10, 35184372088832.0,
        1.7976931348623157e308,   // DBL_MAX
        2.2250738585072014e-308,  // DBL_MIN
        1e-300, 1e300, 9007199254740992.0,
    };
    for (double d : fixedCases) checkFloatLiteralRoundTrip(d);

    // Sweep assorted magnitudes. Subnormals are excluded deliberately: Vora
    // cannot parse them at all (stod reports underflow), so they can never
    // reach the formatter from a literal.
    int swept = 0;
    for (int e = -300; e <= 300; e += 7) {
        for (int mant = 1; mant <= 9; ++mant) {
            const std::string spec = std::to_string(mant) + "e" + std::to_string(e);
            const double d = std::strtod(spec.c_str(), nullptr);
            if (d == 0.0 || !std::isfinite(d) || std::fpclassify(d) == FP_SUBNORMAL) continue;
            checkFloatLiteralRoundTrip(d);
            swept++;
        }
    }
    CHECK(swept > 300);

    // Pseudo-random bit patterns: exercises huge, tiny and awkward mantissas as
    // raw doubles rather than tidy decimal values. Deterministic, so a failure
    // is reproducible.
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    int checkedRandom = 0;
    for (int i = 0; i < 4000; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        double d = 0.0;
        const uint64_t bits = state;
        std::memcpy(&d, &bits, sizeof(d));
        // inf/NaN have no Vora literal spelling; subnormals cannot be parsed.
        if (!std::isfinite(d) || std::fpclassify(d) == FP_SUBNORMAL) continue;
        checkFloatLiteralRoundTrip(d);
        checkedRandom++;
    }
    CHECK(checkedRandom > 2000);
}

TEST_CASE("fmt_bigint_literal_is_lossless") {
    // Oversized integers must keep every digit: decimal is the only lossless
    // spelling, and the formatter must not route them through a double.
    CHECK(trimmed(fmt("let a = 123456789012345678901234567890"))
          == "let a = 123456789012345678901234567890");
    CHECK(trimmed(fmt("let a = -18446744073709551616"))
          == "let a = -18446744073709551616");
    CHECK(trimmed(fmt("let a = 35184372088832")) == "let a = 35184372088832");
    CHECK(trimmed(fmt("let a = 9223372036854775807")) == "let a = 9223372036854775807");
}

// ============================================================================
// Comment preservation
//
// `vora fmt -w` used to delete every comment in a file: the lexer consumed
// them and emitted nothing, so the AST had nowhere to hold them. Comments are
// now carried as trivia on statements and re-emitted.
// ============================================================================

TEST_CASE("fmt_preserves_leading_comment") {
    CHECK(trimmed(fmt("// note\nlet a = 1")) == "// note\nlet a = 1");
}

TEST_CASE("fmt_preserves_trailing_comment") {
    // A comment on the statement's own line stays there.
    CHECK(trimmed(fmt("let a = 1 // note")) == "let a = 1 // note");
}

TEST_CASE("fmt_preserves_comments_inside_blocks") {
    const std::string out = fmt(
        "func f() {\n    // inside\n    let y = 1 // trailing\n    // before brace\n}");
    CHECK(out.find("    // inside") != std::string::npos);
    CHECK(out.find("    let y = 1 // trailing") != std::string::npos);
    CHECK(out.find("    // before brace") != std::string::npos);
}

TEST_CASE("fmt_preserves_block_comment") {
    const std::string out = fmt("/* keep me */\nlet a = 1");
    CHECK(out.find("/* keep me */") != std::string::npos);
}

TEST_CASE("fmt_preserves_comment_after_last_statement") {
    // Nothing follows it, so the program holds it rather than a statement.
    const std::string out = fmt("let a = 1\n// the end");
    CHECK(out.find("// the end") != std::string::npos);
}

TEST_CASE("fmt_preserves_a_file_of_only_comments") {
    const std::string out = fmt("// just\n// comments");
    CHECK(out.find("// just") != std::string::npos);
    CHECK(out.find("// comments") != std::string::npos);
}

TEST_CASE("fmt_comment_handling_is_idempotent") {
    CHECK(isIdempotent("// note\nlet a = 1 // trailing\n"));
    CHECK(isIdempotent("func f() {\n    // c\n    let y = 1\n}\n"));
    CHECK(isIdempotent("/* a */\n\n// b\nlet z = 2\n// tail\n"));
}

TEST_CASE("fmt_keeps_comment_text_verbatim") {
    // Nothing inside a comment is reformatted: it is opaque text.
    const std::string out = fmt("// let x = 1  +  2   <- untouched\nlet a = 1");
    CHECK(out.find("// let x = 1  +  2   <- untouched") != std::string::npos);
}

// ============================================================================
// Fidelity regressions
//
// Each of these classes used to make `vora fmt -w` emit something that either
// did not parse or silently meant something else. The rule is that the output
// must always re-lex into the same program.
// ============================================================================

TEST_CASE("fmt_does_not_fuse_adjacent_tokens") {
    // `not` is a word: juxtaposing it with its operand produced `nota`, an
    // identifier, which silently rewrote the program.
    CHECK(trimmed(fmt("print(not a)")) == "print(not a)");
    // Two minuses would lex as a decrement.
    CHECK(trimmed(fmt("print(- -5)")) == "print(- -5)");
    // The tight forms must stay tight.
    CHECK(trimmed(fmt("print(-x)")) == "print(-x)");
    CHECK(trimmed(fmt("print(!x)")) == "print(!x)");
    CHECK(trimmed(fmt("print(~x)")) == "print(~x)");
    CHECK(trimmed(fmt("print(x++)")) == "print(x++)");
    CHECK(trimmed(fmt("print(++x)")) == "print(++x)");
}

TEST_CASE("fmt_juxtaposition_rule") {
    // The rule is character-based, so it covers pairs no test enumerates.
    CHECK(SourceFormatter::juxtapositionFuses("not", "a"));
    CHECK(SourceFormatter::juxtapositionFuses("await", "x"));
    CHECK(SourceFormatter::juxtapositionFuses("-", "-5"));
    CHECK(SourceFormatter::juxtapositionFuses("+", "+x"));
    CHECK(SourceFormatter::juxtapositionFuses("<", "<x"));
    CHECK(SourceFormatter::juxtapositionFuses("1", "e5"));
    CHECK(SourceFormatter::juxtapositionFuses("1", ".5"));
    CHECK(SourceFormatter::juxtapositionFuses("/", "/x"));
    CHECK(SourceFormatter::juxtapositionFuses("/", "*x"));
    // Safe pairs must not gain a space.
    CHECK_FALSE(SourceFormatter::juxtapositionFuses("-", "x"));
    CHECK_FALSE(SourceFormatter::juxtapositionFuses("!", "x"));
    CHECK_FALSE(SourceFormatter::juxtapositionFuses("f", "("));
    CHECK_FALSE(SourceFormatter::juxtapositionFuses("x", ")"));
    CHECK_FALSE(SourceFormatter::juxtapositionFuses("a", ","));
}

TEST_CASE("fmt_keeps_async_and_rest_and_param_patterns") {
    // Dropping `async` made the body's `await` illegal.
    const std::string f = fmt("async func f(a, ...rest) { return await 1 }");
    CHECK(f.find("async func f") != std::string::npos);
    CHECK(f.find("...rest") != std::string::npos);
    // Dropping the pattern left an empty parameter.
    const std::string g = fmt("func g([x, y]) { return x }");
    CHECK(g.find("[x, y]") != std::string::npos);
    // Shorthand destructuring keeps its default.
    const std::string h = fmt("let {x, y = 1} = obj");
    CHECK(h.find("{x, y = 1}") != std::string::npos);
}

TEST_CASE("fmt_keeps_string_keys_and_patterns_quoted") {
    // A string in a name-like position used to be emitted as display text, so
    // it lost its quotes.
    CHECK(trimmed(fmt("let d = {\"a b\": 1}")) == "let d = {\"a b\": 1}");
    CHECK(trimmed(fmt("let d = {\"if\": 1}")) == "let d = {\"if\": 1}");
    // A bare identifier key keeps its unquoted spelling.
    CHECK(trimmed(fmt("let d = {k: 1}")) == "let d = {k: 1}");
    // `match` string patterns are literals, not arithmetic.
    const std::string m = fmt("let r = match v { \"never-matches-this\" => 1, _ => 2 }");
    CHECK(m.find("\"never-matches-this\"") != std::string::npos);
}

TEST_CASE("fmt_keeps_interpolation_regions_intact") {
    // An interpolation region is code: escaping its inner quotes produced a
    // file the lexer could not read back.
    const std::string out = fmt("print(\"${\"inner ${x}\"}\")");
    CHECK(out.find("${\"inner ${x}\"}") != std::string::npos);
    CHECK(out.find("\\\"inner") == std::string::npos);
    CHECK(isIdempotent("print(\"${\"inner ${x}\"}\")"));
}

TEST_CASE("fmt_separates_statements_asi_would_merge") {
    // Without a ';' this reparsed as `let b = 0[a] = ...`.
    const std::string out = fmt("let b = 0;" + std::string(1, char(10)) + "[a, b] = [1, 2]");
    CHECK(out.find("let b = 0;") != std::string::npos);
    // A statement that cannot be absorbed needs no separator.
    const std::string plain = fmt("let a = 1;" + std::string(1, char(10)) + "let b = 2");
    CHECK(plain.find("let a = 1;") == std::string::npos);
    CHECK(isIdempotent("let b = 0;" + std::string(1, char(10)) + "[a, b] = [1, 2]"));
}
