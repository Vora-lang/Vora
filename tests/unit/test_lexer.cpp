// tests/unit/test_lexer.cpp — Lexer unit tests
//
// Tests: all token types (single-char, two-char), number literals
//        (int/float/hex/oct/bin), strings with escape sequences,
//        keywords, identifiers (including unicode), comments,
//        line/column tracking, error recovery.

#include "doctest.h"
#include "lexer/lexer.h"

using namespace vora;

// Helper: scan source and return tokens.
static std::vector<Token> scan(const std::string& src) {
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    return lexer.scanTokens();
}

// Helper: check that the last token is END_OF_FILE.
static bool endsWithEof(const std::vector<Token>& tokens) {
    return !tokens.empty() && tokens.back().type == TokenType::END_OF_FILE;
}

// ============================================================================
// Single-character tokens
// ============================================================================

TEST_CASE("lexer_single_char_parens") {
    auto t = scan("()");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::LEFT_PAREN);
    CHECK(t[1].type == TokenType::RIGHT_PAREN);
    CHECK(endsWithEof(t));
}

TEST_CASE("lexer_single_char_braces") {
    auto t = scan("{}");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::LEFT_BRACE);
    CHECK(t[1].type == TokenType::RIGHT_BRACE);
}

TEST_CASE("lexer_single_char_brackets") {
    auto t = scan("[]");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::LEFT_BRACKET);
    CHECK(t[1].type == TokenType::RIGHT_BRACKET);
}

TEST_CASE("lexer_single_char_punctuation") {
    auto t = scan(",.:?;");
    REQUIRE(t.size() >= 6);
    CHECK(t[0].type == TokenType::COMMA);
    CHECK(t[1].type == TokenType::DOT);
    CHECK(t[2].type == TokenType::COLON);
    CHECK(t[3].type == TokenType::QUESTION);
    CHECK(t[4].type == TokenType::SEMICOLON);
}

// ============================================================================
// Two-character tokens
// ============================================================================

TEST_CASE("lexer_two_char_comparison") {
    auto t = scan("== != <= >=");
    REQUIRE(t.size() >= 5);
    CHECK(t[0].type == TokenType::EQUAL_EQUAL);
    CHECK(t[1].type == TokenType::NOT_EQUAL);
    CHECK(t[2].type == TokenType::LESS_EQUAL);
    CHECK(t[3].type == TokenType::GREATER_EQUAL);
}

TEST_CASE("lexer_two_char_inc_dec_power") {
    auto t = scan("++ -- **");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::PLUS_PLUS);
    CHECK(t[1].type == TokenType::MINUS_MINUS);
    CHECK(t[2].type == TokenType::POWER);
}

TEST_CASE("lexer_two_char_logical") {
    auto t = scan("&& ||");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::AND);
    CHECK(t[1].type == TokenType::OR);
}

TEST_CASE("lexer_compound_assignment") {
    auto t = scan("+= -= *= /= %=");
    REQUIRE(t.size() >= 6);
    CHECK(t[0].type == TokenType::PLUS_EQUAL);
    CHECK(t[1].type == TokenType::MINUS_EQUAL);
    CHECK(t[2].type == TokenType::MULTIPLY_EQUAL);
    CHECK(t[3].type == TokenType::DIVIDE_EQUAL);
    CHECK(t[4].type == TokenType::MODULO_EQUAL);
}

TEST_CASE("lexer_power_equal") {
    // '**=' is one token, not POWER + EQUAL
    auto t = scan("**=");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::POWER_EQUAL);

    // '**' and '*=' still lex correctly around the new token
    auto u = scan("2 ** 3");
    CHECK(u[0].type == TokenType::NUMBER);
    CHECK(u[1].type == TokenType::POWER);
    CHECK(u[2].type == TokenType::NUMBER);

    auto v = scan("2 *=");
    CHECK(v[1].type == TokenType::MULTIPLY_EQUAL);
}

TEST_CASE("lexer_two_equals_vs_equal_equal") {
    // "= =" — two separate EQUAL tokens (space-separated)
    auto t = scan("= =");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::EQUAL);
    CHECK(t[1].type == TokenType::EQUAL);
}

// ============================================================================
// Number literals
// ============================================================================

TEST_CASE("lexer_integer_literal") {
    auto t = scan("0 42 1234567890");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "0");
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "42");
    CHECK(t[2].type == TokenType::NUMBER);
}

TEST_CASE("lexer_float_literal") {
    auto t = scan("3.14 0.5 10.0");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "3.14");
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "0.5");
    CHECK(t[2].type == TokenType::NUMBER);
    CHECK(t[2].lexeme == "10.0");
}

TEST_CASE("lexer_exponent_literal") {
    // Exponent literals: e/E, optional sign, at least one digit.
    auto t1 = scan("1e10");
    REQUIRE(t1.size() >= 2);
    CHECK(t1[0].type == TokenType::NUMBER);
    CHECK(t1[0].lexeme == "1e10");

    auto t2 = scan("2.5e-3");
    REQUIRE(t2.size() >= 2);
    CHECK(t2[0].type == TokenType::NUMBER);
    CHECK(t2[0].lexeme == "2.5e-3");

    auto t3 = scan("1E+5");
    REQUIRE(t3.size() >= 2);
    CHECK(t3[0].lexeme == "1E+5");

    auto t4 = scan("6.02e23");
    CHECK(t4[0].lexeme == "6.02e23");

    // A leading sign is a separate token, as for every other number.
    auto t5 = scan("-1e10");
    REQUIRE(t5.size() >= 3);
    CHECK(t5[0].type == TokenType::MINUS);
    CHECK(t5[1].lexeme == "1e10");
}

TEST_CASE("lexer_exponent_requires_digits") {
    // `1e` is NUMBER(1) followed by IDENTIFIER(e): an exponent needs digits, and
    // swallowing the identifier would break `1e` where `e` is a variable.
    auto t = scan("1e");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "1");
    CHECK(t[1].type == TokenType::IDENTIFIER);
    CHECK(t[1].lexeme == "e");

    // Same for a bare sign with no digits behind it.
    auto t2 = scan("1e+");
    REQUIRE(t2.size() >= 4);
    CHECK(t2[0].lexeme == "1");
    CHECK(t2[1].type == TokenType::IDENTIFIER);
    CHECK(t2[1].lexeme == "e");
    CHECK(t2[2].type == TokenType::PLUS);
}

TEST_CASE("lexer_hex_exponent_is_not_an_exponent") {
    // Hex digits include 'e', so 0x1e5 must stay a single hex literal.
    auto t = scan("0x1e5");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "0x1e5");
}

TEST_CASE("lexer_dot_number_boundary") {
    // ".5" — dot is DOT token, then "5" is NUMBER
    auto t = scan(".5");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::DOT);
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "5");
}

TEST_CASE("lexer_hex_literal") {
    auto t = scan("0xFF 0x1A2B 0xdeadbeef 0XABC");
    REQUIRE(t.size() >= 5);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "0xFF");
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "0x1A2B");
    CHECK(t[2].type == TokenType::NUMBER);
    CHECK(t[3].type == TokenType::NUMBER);
    CHECK(t[3].lexeme == "0XABC");
}

TEST_CASE("lexer_octal_literal") {
    auto t = scan("0o777 0o10 0O77");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "0o777");
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "0o10");
    CHECK(t[2].type == TokenType::NUMBER);
    CHECK(t[2].lexeme == "0O77");
}

TEST_CASE("lexer_binary_literal") {
    auto t = scan("0b1010 0b0 0B1");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "0b1010");
    CHECK(t[1].type == TokenType::NUMBER);
    CHECK(t[1].lexeme == "0b0");
    CHECK(t[2].type == TokenType::NUMBER);
    CHECK(t[2].lexeme == "0B1");
}

// ============================================================================
// Strings
// ============================================================================

TEST_CASE("lexer_string_double_quoted") {
    auto t = scan("\"hello\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::STRING);
    CHECK(t[0].lexeme == "hello");
}

TEST_CASE("lexer_string_single_quoted") {
    auto t = scan("'world'");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::STRING);
    CHECK(t[0].lexeme == "world");
}

TEST_CASE("lexer_string_empty") {
    auto t = scan("\"\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::STRING);
    CHECK(t[0].lexeme == "");
}

TEST_CASE("lexer_string_escape_newline") {
    auto t = scan("\"\\n\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\n");
}

TEST_CASE("lexer_string_escape_tab") {
    auto t = scan("\"\\t\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\t");
}

TEST_CASE("lexer_string_escape_carriage_return") {
    auto t = scan("\"\\r\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\r");
}

TEST_CASE("lexer_string_escape_null") {
    auto t = scan("\"\\0\"");
    REQUIRE(t.size() >= 2);
    std::string lexeme = t[0].lexeme;
    // Should contain a null character
    CHECK(lexeme.size() == 1);
    CHECK(lexeme[0] == '\0');
}

TEST_CASE("lexer_string_escape_backslash") {
    auto t = scan("\"\\\\\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\\");
}

TEST_CASE("lexer_string_escape_quote") {
    auto t = scan("\"\\\"\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\"");
}

TEST_CASE("lexer_string_unknown_escape_kept") {
    // Unknown escape like \z: both \ and z kept
    auto t = scan("\"\\z\"");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].lexeme == "\\z");
}

TEST_CASE("lexer_string_unterminated") {
    // Unterminated string — should emit best-effort token + report error
    auto t = scan("\"hello");
    REQUIRE(t.size() >= 2);
    // Best-effort: the string content without the closing quote
    CHECK(t[0].type == TokenType::STRING);
}

// ============================================================================
// Keywords
// ============================================================================

TEST_CASE("lexer_keywords_not_identifiers") {
    const char* keywords[] = {
        "let", "func", "return", "if", "else", "while", "for", "in",
        "class", "this", "break", "continue", "try", "catch", "finally",
        "throw", "true", "false", "null", "and", "or", "not"
    };
    for (const char* kw : keywords) {
        auto t = scan(kw);
        INFO("Keyword: " << kw);
        REQUIRE(t.size() >= 2);
        CHECK_FALSE(t[0].type == TokenType::IDENTIFIER);
        CHECK(t[0].lexeme == kw);
    }
}

TEST_CASE("lexer_not_maps_to_not_token") {
    // `not` is an alias for `!` — same TokenType, so the parser and
    // compiler need no changes (syntax-review #2.9).
    auto t = scan("not");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::NOT);

    auto bang = scan("!");
    REQUIRE(bang.size() >= 2);
    CHECK(bang[0].type == TokenType::NOT);

    // Still allowed as a prefix of a longer identifier.
    auto ident = scan("nothing");
    REQUIRE(ident.size() >= 2);
    CHECK(ident[0].type == TokenType::IDENTIFIER);
    CHECK(ident[0].lexeme == "nothing");
}

// ============================================================================
// Identifiers
// ============================================================================

TEST_CASE("lexer_simple_identifier") {
    auto t = scan("foo _bar x1 camelCase");
    REQUIRE(t.size() >= 5);
    CHECK(t[0].type == TokenType::IDENTIFIER);
    CHECK(t[0].lexeme == "foo");
    CHECK(t[1].type == TokenType::IDENTIFIER);
    CHECK(t[1].lexeme == "_bar");
    CHECK(t[2].type == TokenType::IDENTIFIER);
    CHECK(t[2].lexeme == "x1");
    CHECK(t[3].type == TokenType::IDENTIFIER);
    CHECK(t[3].lexeme == "camelCase");
}

// ============================================================================
// Comments
// ============================================================================

TEST_CASE("lexer_line_comment") {
    auto t = scan("// this is a comment\n42");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "42");
}

TEST_CASE("lexer_line_comment_end_of_file") {
    auto t = scan("// comment at EOF");
    // Only EOF token
    CHECK(t.size() == 1);
    CHECK(t[0].type == TokenType::END_OF_FILE);
}

TEST_CASE("lexer_block_comment") {
    auto t = scan("/* comment */ 42");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "42");
}

TEST_CASE("lexer_nested_block_comment") {
    auto t = scan("/* outer /* inner */ end */ 7");
    REQUIRE(t.size() >= 2);
    CHECK(t[0].type == TokenType::NUMBER);
    CHECK(t[0].lexeme == "7");
}

// ============================================================================
// Line and column tracking
// ============================================================================

TEST_CASE("lexer_line_column_single_line") {
    auto t = scan("let x = 1;");
    REQUIRE(t.size() >= 5);
    CHECK(t[0].line == 1);
    CHECK(t[0].column == 1);  // "let" starts at column 1
    CHECK(t[1].line == 1);
    CHECK(t[1].column == 5);  // "x" at column 5
    CHECK(t[2].line == 1);
    CHECK(t[2].column == 7);  // "=" at column 7
    CHECK(t[3].line == 1);
    CHECK(t[3].column == 9);  // "1" at column 9
}

TEST_CASE("lexer_line_column_multi_line") {
    // Use an actual newline, not \\n escape
    auto t = scan("let x = 1;\nlet y = 2;");
    // First line tokens
    CHECK(t[0].line == 1);
    CHECK(t[0].column == 1);  // "let"
    // Second line: "let" is at index 5 (after ';' at index 4)
    CHECK(t[5].line == 2);  // "let" on line 2
    CHECK(t[5].column == 1);
}

// ============================================================================
// Bitwise operators (P1-F)
// ============================================================================

TEST_CASE("lexer_bitwise_single_char") {
    auto t = scan("& ^ ~");
    REQUIRE(t.size() >= 4);
    CHECK(t[0].type == TokenType::AMPERSAND);
    CHECK(t[1].type == TokenType::CARET);
    CHECK(t[2].type == TokenType::TILDE);
}

TEST_CASE("lexer_bitwise_shift") {
    auto t = scan("<< >>");
    REQUIRE(t.size() >= 3);
    CHECK(t[0].type == TokenType::LESS_LESS);
    CHECK(t[1].type == TokenType::GREATER_GREATER);
}

TEST_CASE("lexer_bitwise_vs_logical") {
    // '&&' is AND, '&' is AMPERSAND; '||' is OR, '|' is PIPE.
    auto t = scan("&& & || |");
    REQUIRE(t.size() >= 5);
    CHECK(t[0].type == TokenType::AND);
    CHECK(t[1].type == TokenType::AMPERSAND);
    CHECK(t[2].type == TokenType::OR);
    CHECK(t[3].type == TokenType::PIPE);
}

