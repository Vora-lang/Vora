/**
 * @file src/lexer/lexer.h
 * @brief Lexer (scanner) for the Vora scripting language.
 *
 * Converts raw source text into a flat sequence of Tokens consumed by the
 * parser.  The lexer is a hand-written character-at-a-time scanner that
 * tokenizes identifiers, keywords, numbers (decimal, hex, octal, binary),
 * strings (single- and double-quoted with escape sequences), operators
 * (including multi-character operators like &&, <=, **, ..=), and comments
 * (line // and nestable block /* *​/).
 *
 * Error recovery is best-effort: on an unexpected character the scanner
 * reports the error through the ErrorReporter and continues, so the parser
 * always receives a complete token stream (including END_OF_FILE).
 */

#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "token.h"
#include "../common/error_reporter.h"

namespace vora {

/**
 * @brief Hand-written character-at-a-time lexer for Vora source code.
 *
 * The Lexer scans source text character by character and produces a flat
 * vector of Token objects.  It handles:
 *
 * - Whitespace (spaces, tabs, carriage returns are skipped; newlines track
 *   line/column for diagnostics).
 * - Single-character tokens: ( ) { } [ ] , . : ; ? + - * / % = < > ! & |
 * - Multi-character operators: ++ -- ** <= >= == != && || += -= *= /= %=
 *   => .. ... ..= ?. ??
 * - Numeric literals: decimal (123, 3.14), hex (0xFF), octal (0o77),
 *   binary (0b1010).
 * - String literals: double-quoted ("...") and single-quoted ('...'), both
 *   supporting C-style escape sequences (\\n, \\t, \\r, \\\\, \\", \\', \\0).
 * - Identifiers and keywords: identifiers are greedily scanned, then looked
 *   up in the keyword table; unmatched identifiers receive IDENTIFIER.
 * - Comments: line comments (//) run to end of line; block comments (/* *​/)
 *   are nestable (depth-tracked) with unterminated-block detection.
 *
 * The scanner never throws.  Errors are reported through the ErrorReporter
 * interface, and tokenization continues so the parser always receives a
 * complete stream (including the terminating END_OF_FILE).
 */
class Lexer {
public:
    /**
     * @brief Construct a Lexer for the given source text.
     *
     * @param source   The full source code string (taken by value, then moved).
     * @param reporter Error reporter for diagnostic emission (lexer errors
     *                 are always Severity::Error).
     */
    Lexer(std::string source, ErrorReporter& reporter);

    /**
     * @brief Scan the entire source and return the resulting token stream.
     *
     * Iterates through the source character by character until all input is
     * consumed, then appends a final END_OF_FILE token.
     *
     * @return std::vector<Token> The complete token sequence, including
     *         END_OF_FILE as the last element.
     */
    std::vector<Token> scanTokens();

    /**
     * @brief Check whether any errors were encountered during scanning.
     *
     * Delegates to the ErrorReporter's hadError() method, which returns true
     * if at least one Severity::Error diagnostic was emitted.
     *
     * @return true if at least one error was reported.
     */
    bool hasError() const { return reporter_.hadError(); }

private:
    /// @brief The full source code text being scanned.
    std::string source;

    /// @brief Error reporter for emitting diagnostics.
    ErrorReporter& reporter_;

    /// @brief Accumulated token list (built incrementally by scanTokens).
    std::vector<Token> tokens;

    /// @brief Start offset (in bytes) of the current lexeme being scanned.
    size_t start = 0;

    /// @brief Current position (byte offset) in the source text.
    size_t current = 0;

    /// @brief Current line number (1-indexed), tracked for diagnostics.
    int line = 1;

    /// @brief Column of the current scan position (1-indexed byte offset within line).
    int column = 1;

    /// @brief Column at `start` — saved when a new lexeme begins, used for Token::column.
    int startColumn = 1;

private:
    /// @brief Lookup table mapping keyword strings to their TokenType values.
    static const std::unordered_map<std::string, TokenType> keywords;

private:
    /**
     * @brief Test whether the scanner has consumed all input.
     *
     * @return true when `current >= source.length()`.
     */
    bool isAtEnd() const;

    /**
     * @brief Consume and return the current character, advancing the scanner.
     *
     * Increments `column` and moves `current` forward by one byte.
     *
     * @return The character at the position before advancing.
     */
    char advance();

    /**
     * @brief Append a token of the given type using the current lexeme span.
     *
     * The lexeme text is extracted via `source.substr(start, current - start)`.
     * The token's line and column come from the saved `line` and `startColumn`.
     *
     * @param type The TokenType to assign to the new token.
     */
    void addToken(TokenType type);

    /**
     * @brief Scan a numeric literal (decimal, hex, octal, or binary).
     *
     * Handles:
     * - Decimal integers and floats (including fractional part after '.').
     * - Hex literals: 0x / 0X prefix followed by hex digits.
     * - Octal literals: 0o / 0O prefix followed by digits 0-7.
     * - Binary literals: 0b / 0B prefix followed by digits 0-1.
     *
     * Reports an error for prefixes with no following digits (e.g., "0x").
     * Emits a NUMBER token on success.
     */
    void number();

    /**
     * @brief Scan a string literal delimited by the given quote character.
     *
     * Supports C-style escape sequences: \\n, \\t, \\r, \\\\, \\", \\', \\0.
     * Unrecognized escape characters are kept literally (both backslash and
     * the following character).  Newlines inside strings increment the line
     * counter.  Reports an error on unterminated strings but still emits a
     * STRING token with the text scanned so far (best-effort recovery).
     *
     * @param delimiter The quote character: '"' for double-quoted strings,
     *                  '\'' for single-quoted strings.
     */
    void string(char delimiter);

    /**
     * @brief Scan an identifier or keyword.
     *
     * Consumes alphanumeric characters and underscores (and UTF-8 continuation
     * bytes).  After scanning, looks up the lexeme in the keyword table:
     * if found, emits the keyword's TokenType; otherwise emits IDENTIFIER.
     */
    void identifier();

    /**
     * @brief Test whether a character can start an identifier.
     *
     * Returns true for ASCII letters (a-z, A-Z), underscore (_), and any byte
     * with the high bit set (UTF-8 multi-byte lead byte).
     *
     * @param c The character to test.
     * @return true if the character is valid as the first character of an identifier.
     */
    bool isAlpha(char c) const;

    /**
     * @brief Test whether a character is valid inside an identifier.
     *
     * Returns true for identifier-start characters (isAlpha) plus ASCII digits (0-9).
     *
     * @param c The character to test.
     * @return true if the character is a valid identifier continuation character.
     */
    bool isAlphaNumeric(char c) const;

    /**
     * @brief Core tokenization dispatch — scan a single token from the current position.
     *
     * Reads one character via advance() and dispatches based on its value:
     * single-char tokens, multi-char operator lookahead, whitespace/newline
     * handling, string delimiters, or delegation to number()/identifier().
     * On an unrecognized character, reports an error and continues.
     */
    void scanToken();

    /**
     * @brief Return the current character without consuming it.
     *
     * @return The character at position `current`, or '\\0' if at end of file.
     */
    char peek() const;

    /**
     * @brief Return the character one past the current position without consuming.
     *
     * Used for two-character lookahead (e.g., distinguishing ".." from "...").
     *
     * @return The character at `current + 1`, or '\\0' if past end of file.
     */
    char peekNext() const;

    /**
     * @brief Conditionally consume the next character if it matches the expected value.
     *
     * Advances `current` and `column` only when `source[current] == expected`.
     *
     * @param expected The character to test against the current position.
     * @return true if the character matched and was consumed.
     */
    bool match(char expected);

    /**
     * @brief Report a lexer error at the current position.
     *
     * Emits a Severity::Error diagnostic via the ErrorReporter at the current
     * line and column with an underline length of 1.
     *
     * @param message Human-readable error message.
     */
    void error(const std::string& message);

    /**
     * @brief Skip a line comment (// to end of line).
     *
     * Consumes all characters until a newline or EOF is encountered.
     * Does not emit a token.
     */
    void lineComment();

    /**
     * @brief Skip a nestable block comment (/* ... *​/).
     *
     * Tracks nesting depth so that nested block comments (/* /* *​/ *​/) are
     * handled correctly.  Tracks newlines for line counting.  Reports an
     * error if the comment is unterminated (EOF reached with depth > 0).
     * Does not emit a token.
     */
    void blockComment();
};

}
