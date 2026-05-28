#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "token.h"

namespace vora {

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> scanTokens();

private:
    std::string source;

    std::vector<Token> tokens;

    size_t start = 0;
    size_t current = 0;

    int line = 1;

private:
    static const std::unordered_map<std::string, TokenType> keywords;

private:
    bool isAtEnd() const;

    char advance();

    void addToken(TokenType type);

    void number();

    void string();

    void identifier();

    bool isAlpha(char c) const;

    bool isAlphaNumeric(char c) const;

    void scanToken();

    char peek() const;

    char peekNext() const;

    bool match(char expected);

    void lineComment();

    void blockComment();
};

}
