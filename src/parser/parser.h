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

    bool hasError() const { return hadError; }

private:
    std::vector<Token> tokens;

    size_t current = 0;

    bool hadError = false;

private:

    void error(const std::string& message);
    void synchronize();
    std::unique_ptr<Expr> expression();

    std::unique_ptr<Expr> parsePrecedence(int precedence);

    std::unique_ptr<Expr> primary();

    std::unique_ptr<Expr> call();

    std::unique_ptr<Expr> finishCall(
        std::unique_ptr<Expr> callee
    );

    std::unique_ptr<Stmt> statement();

    std::unique_ptr<Stmt> letStatement();

    std::unique_ptr<BlockStmt> blockStatement();

    std::unique_ptr<Stmt> returnStatement();

    std::unique_ptr<Stmt> ifStatement();

    std::unique_ptr<Stmt> whileStatement();

    std::unique_ptr<Stmt> forStatement();

    std::unique_ptr<Stmt> funcStatement();

    std::unique_ptr<Stmt> objStatement();

    std::unique_ptr<Stmt> breakStatement();

    std::unique_ptr<Stmt> continueStatement();

    std::unique_ptr<Stmt> tryStatement();

    std::unique_ptr<Stmt> throwStatement();

private:
    bool isAtEnd() const;

    Token advance();

    Token peek() const;

    Token previous() const;

    bool match(TokenType type);

    bool check(TokenType type) const;

    int getPrecedence(TokenType type) const;
};

}
