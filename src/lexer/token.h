#pragma once

#include <string>

namespace vora {

    enum class TokenType {
        // Parentheses
        LEFT_PAREN, // (
        RIGHT_PAREN, // )

        // Braces
        LEFT_BRACE, // {
        RIGHT_BRACE, // }

        // Brackets
        LEFT_BRACKET, // [
        RIGHT_BRACKET, // ]

        // Arithmetic
        PLUS, // +
        MINUS, // -
        MULTIPLY, // *
        DIVIDE, // /
        MODULO, // %

        // Assignment / compare
        EQUAL, // =
        LESS, // <
        GREATER, // >
        PLUS_PLUS, // ++
        MINUS_MINUS, // --
        POWER, // **

        // Compound assignment
        PLUS_EQUAL, // +=
        MINUS_EQUAL, // -=
        MULTIPLY_EQUAL, // *=
        DIVIDE_EQUAL, // /=
        MODULO_EQUAL, // %=

        EQUAL_EQUAL, // ==
        LESS_EQUAL, // <=
        GREATER_EQUAL, // >=
        NOT_EQUAL, // !=
        AND, // &&
        OR, // ||
        NOT, // !
        TRUE,
        FALSE,
        NULL_TOKEN,

        // Punctuation
        COMMA, // ,
        DOT, // .
        DOT_DOT_DOT, // ...
        COLON, // :
        QUESTION, // ?
        SEMICOLON, // ;

        // Literals
        NUMBER, //
        STRING, //
        IDENTIFIER, //

        // Keywords
        LET, // let
        CONST, // const
        FUNC, // func
        RETURN, // return
        IF, // if
        ELSE, // else
        WHILE, // while
        FOR, // for
        IN, // in
        OBJ, // Obj
        THIS, // this
        SUPER, // super
        BREAK, // break
        CONTINUE, // continue
        TRY, // try
        CATCH, // catch
        FINALLY, // finally
        THROW, // throw
        IMPORT, // import
        EXPORT, // export
        AS, // as
        FROM, // from
        YIELD, // yield
        MATCH, // match

        // Special
        FAT_ARROW, // =>
        DOT_DOT, // ..
        DOT_DOT_EQUAL, // ..=
        END_OF_FILE, //
        INVALID //
    };

std::string tokenTypeToString(TokenType type);

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;

    Token() : type(TokenType::INVALID), lexeme(""), line(0), column(0) {}

    Token(TokenType type, std::string lexeme, int line, int column)
        : type(type),
          lexeme(std::move(lexeme)),
          line(line),
          column(column) {
    }
    std::string toString() const;
};

}
