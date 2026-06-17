#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "token.h"
#include "../common/error_reporter.h"

namespace vora {

class Lexer {
public:
    Lexer(std::string source, ErrorReporter& reporter);

    std::vector<Token> scanTokens();

    bool hasError() const { return reporter_.hadError(); }

private:
    std::string source;
    ErrorReporter& reporter_;

    std::vector<Token> tokens;

    size_t start = 0;
    size_t current = 0;

    int line = 1;
    int column = 1;       // current position column
    int startColumn = 1;  // column at `start` (used for token creation)

private:
    static const std::unordered_map<std::string, TokenType> keywords;

private:
    bool isAtEnd() const;

    char advance();

    void addToken(TokenType type);

    void number();

    void string(char delimiter);

    void identifier();

    bool isAlpha(char c) const;

    bool isAlphaNumeric(char c) const;

    void scanToken();

    char peek() const;

    char peekNext() const;

    bool match(char expected);

    void error(const std::string& message);

    void lineComment();

    void blockComment();
};

}
