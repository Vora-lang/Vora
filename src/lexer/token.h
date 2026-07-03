/**
 * @file token.h
 * @brief Lexer token types and Token data structure.
 *
 * Defines the TokenType enumeration of all possible token kinds, the Token
 * struct that bundles a type, lexeme, and source location, and the
 * tokenTypeToString() helper for human-readable token type names.
 */

#pragma once

#include <string>

namespace vora {

    /**
     * @brief Enumeration of all token types produced by the lexer.
     *
     * Each value represents a distinct lexical category: literal values
     * (numbers, strings, identifiers), keywords, operators, delimiters,
     * and special sentinel tokens (END_OF_FILE, INVALID).
     */
    enum class TokenType {
        // ---- Grouping delimiters ----

        LEFT_PAREN,  ///< '('  Left parenthesis — grouping, function call args
        RIGHT_PAREN, ///< ')'  Right parenthesis

        LEFT_BRACE,  ///< '{'  Left brace — block statement, object literal
        RIGHT_BRACE, ///< '}'  Right brace

        LEFT_BRACKET,  ///< '['  Left bracket — array literal, index access
        RIGHT_BRACKET, ///< ']'  Right bracket

        // ---- Arithmetic operators ----

        PLUS,     ///< '+'  Addition, unary plus
        MINUS,    ///< '-'  Subtraction, unary negation
        MULTIPLY, ///< '*'  Multiplication
        DIVIDE,   ///< '/'  Division
        MODULO,   ///< '%'  Modulo (remainder)

        // ---- Assignment / comparison base tokens ----

        EQUAL,        ///< '='  Assignment
        LESS,         ///< '<'  Less-than comparison
        GREATER,      ///< '>'  Greater-than comparison
        PLUS_PLUS,    ///< '++'  Postfix/prefix increment
        MINUS_MINUS,  ///< '--'  Postfix/prefix decrement
        POWER,        ///< '**'  Power (exponentiation) operator

        // ---- Compound assignment operators ----

        PLUS_EQUAL,     ///< '+='  Add-assign
        MINUS_EQUAL,    ///< '-='  Subtract-assign
        MULTIPLY_EQUAL, ///< '*='  Multiply-assign
        DIVIDE_EQUAL,   ///< '/='  Divide-assign
        MODULO_EQUAL,   ///< '%='  Modulo-assign

        // ---- Comparison / logical operators ----

        EQUAL_EQUAL,   ///< '=='  Equality comparison
        LESS_EQUAL,    ///< '<='  Less-than-or-equal comparison
        GREATER_EQUAL, ///< '>='  Greater-than-or-equal comparison
        NOT_EQUAL,     ///< '!='  Not-equal comparison
        AND,           ///< '&&'  Logical AND (short-circuit)
        OR,            ///< '||'  Logical OR (short-circuit)
        NOT,           ///< '!'  Logical NOT

        /// @name Boolean and null literals
        /// @{
        TRUE,       ///< 'true'  Boolean true literal
        FALSE,      ///< 'false'  Boolean false literal
        NULL_TOKEN, ///< 'null'  Null literal
        /// @}

        // ---- Punctuation / delimiters ----

        COMMA,            ///< ','  Comma — separator in arg lists, arrays, dicts
        DOT,              ///< '.'  Dot — property access, decimal point
        DOT_DOT_DOT,      ///< '...'  Rest parameter / spread operator
        COLON,            ///< ':'  Colon — dict key-value separator, ternary else
        QUESTION,         ///< '?'  Question mark — ternary conditional
        QUESTION_QUESTION,///< '??'  Null-coalescing operator
        QUESTION_DOT,     ///< '?.'  Optional chaining
        SEMICOLON,        ///< ';'  Semicolon — statement terminator

        // ---- Literal value tokens ----

        NUMBER,     ///< Numeric literal (integer or floating-point)
        STRING,     ///< String literal (single- or double-quoted)
        IDENTIFIER, ///< User-defined identifier name

        // ---- Reserved keywords ----

        LET,      ///< 'let'     Mutable variable declaration
        CONST,    ///< 'const'   Immutable variable declaration
        FUNC,     ///< 'func'    Function declaration / expression
        RETURN,   ///< 'return'  Return from function
        IF,       ///< 'if'      Conditional branch
        ELSE,     ///< 'else'    Alternative conditional branch
        WHILE,    ///< 'while'   While loop
        FOR,      ///< 'for'     For-in / C-style for loop
        IN,       ///< 'in'      Iterator keyword (for-in loop)
        OBJ,      ///< 'Obj'     Object / class definition
        THIS,     ///< 'this'    Reference to current instance
        SUPER,    ///< 'super'   Reference to parent class
        BREAK,    ///< 'break'   Exit innermost loop or match arm
        CONTINUE, ///< 'continue' Skip to next loop iteration
        TRY,      ///< 'try'     Begin guarded block
        CATCH,    ///< 'catch'   Exception handler
        FINALLY,  ///< 'finally' Guaranteed cleanup block
        THROW,    ///< 'throw'   Raise an exception
        IMPORT,   ///< 'import'  Module import
        EXPORT,   ///< 'export'  Module export
        AS,       ///< 'as'      Import alias, type cast
        FROM,     ///< 'from'    Import source specifier
        YIELD,    ///< 'yield'   Generator yield
        ASYNC,    ///< 'async'   Async function declaration
        AWAIT,    ///< 'await'   Await expression
        MATCH,    ///< 'match'   Pattern matching
        DEFER,    ///< 'defer'   Deferred execution
        DO,       ///< 'do'      Do-while loop (or expression block)

        // ---- Special operator tokens ----

        FAT_ARROW,      ///< '=>'  Fat arrow — function expression / match arm
        DOT_DOT,        ///< '..'  Range operator (exclusive upper bound)
        DOT_DOT_EQUAL,  ///< '..=' Range operator (inclusive upper bound)

        // ---- Sentinel tokens ----

        END_OF_FILE, ///< End of input stream (no more tokens available)
        INVALID      ///< Unrecognized / illegal character sequence
    };

/**
 * @brief  Convert a TokenType enum value to its human-readable string form.
 * @param  type  The token type to stringify.
 * @return A string like "LEFT_PAREN", "IF", "NUMBER", or "UNKNOWN" for
 *         out-of-range values.
 */
std::string tokenTypeToString(TokenType type);

/**
 * @brief Represents a single lexer token with type, source text, and location.
 *
 * Every token produced by the lexer carries a TokenType categorisation, the
 * original source text (lexeme), and its 1-based line/column position for
 * error reporting.
 */
struct Token {
    TokenType type;   ///< Kind of token (keyword, operator, literal, etc.)
    std::string lexeme; ///< Original source text that produced this token
    int line;         ///< 1-based line number where the token starts
    int column;       ///< 1-based column (byte offset) where the token starts

    /// Default constructor — creates an INVALID token at (0,0).
    Token() : type(TokenType::INVALID), lexeme(""), line(0), column(0) {}

    /// Full constructor.
    /// @param type   Token kind.
    /// @param lexeme Original source text.
    /// @param line   1-based line number.
    /// @param column 1-based column number.
    Token(TokenType type, std::string lexeme, int line, int column)
        : type(type),
          lexeme(std::move(lexeme)),
          line(line),
          column(column) {
    }

    /// @brief  Format token as "TokenType(lexeme) at line:column".
    /// @return Human-readable, one-line representation of the token.
    std::string toString() const;
};

}
