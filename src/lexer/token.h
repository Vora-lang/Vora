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
        COLON, // :
        SEMICOLON, // ;

        // Literals
        NUMBER, //
        STRING, //
        IDENTIFIER, //

        // Keywords
        LET, // let
        FUNC, // func
        RETURN, // return
        IF, // if
        ELSE, // else
        WHILE, // while

        // Special
        END_OF_FILE, //
        INVALID //
    };

std::string tokenTypeToString(TokenType type);

struct Token {
    TokenType type;
    std::string lexeme;
    int line;

    Token(TokenType type, std::string lexeme, int line)
        : type(type),
          lexeme(std::move(lexeme)),
          line(line) {
    }
};

}
