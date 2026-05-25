#pragma once

#include <vector>
#include <memory>

#include "../lexer/token.h"
#include "../ast/expr.h"

namespace vora {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    std::unique_ptr<Expr> parse();

private:
    std::vector<Token> tokens;

    size_t current = 0;

private:
    std::unique_ptr<Expr> expression();

    std::unique_ptr<Expr> parsePrecedence(int precedence);

    std::unique_ptr<Expr> primary();

private:
    bool isAtEnd() const;

    Token advance();

    Token peek() const;

    Token previous() const;

    bool match(TokenType type);

    int getPrecedence(TokenType type) const;
};

}
