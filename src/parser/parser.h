#pragma once

#include <vector>
#include <memory>

#include "../lexer/token.h"
#include "../ast/expr.h"
#include "../ast/stmt.h"
#include "../ast/program.h"

namespace vora {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    std::unique_ptr<Program> parse();

private:
    std::vector<Token> tokens;

    size_t current = 0;

private:
    std::unique_ptr<Expr> expression();

    std::unique_ptr<Expr> parsePrecedence(int precedence);

    std::unique_ptr<Expr> primary();

    std::unique_ptr<Stmt> statement();

    std::unique_ptr<Stmt> letStatement();

    std::unique_ptr<Stmt> blockStatement();

    std::unique_ptr<Stmt> returnStatement();

    std::unique_ptr<Stmt> ifStatement();

    std::unique_ptr<Stmt> whileStatement();

private:
    bool isAtEnd() const;

    Token advance();

    Token peek() const;

    Token previous() const;

    bool match(TokenType type);

    int getPrecedence(TokenType type) const;
};

}
