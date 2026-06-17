#pragma once

#include <vector>
#include <memory>

#include "../lexer/token.h"
#include "../ast/expr.h"
#include "../ast/stmt.h"
#include "../ast/program.h"
#include "../common/error_reporter.h"

namespace vora {

class Parser {
public:
    Parser(std::vector<Token> tokens, ErrorReporter& reporter);

    std::unique_ptr<Program> parse();

    bool hasError() const { return reporter_.hadError(); }

    // Set source text for error display (parser errors show source snippets).
    void setSource(const std::string& source) { sourceText = source; }

private:
    std::vector<Token> tokens;
    std::string sourceText;
    ErrorReporter& reporter_;

    size_t current = 0;

private:

    void error(const std::string& message);

    // Error recovery helpers for error-tolerant parsing.
    // Call error(), synchronize to next statement boundary, and return an
    // ErrorStmt placeholder so the AST retains structural coverage even
    // for malformed source (essential for LSP features).
    std::unique_ptr<Stmt> errorStmt(const std::string& message);
    std::unique_ptr<Expr> errorExpr(const std::string& message);

    void synchronize();
    std::unique_ptr<Expr> expression();

    std::unique_ptr<Expr> parsePrecedence(int precedence);

    std::unique_ptr<Expr> primary();

    std::unique_ptr<Expr> call();

    std::unique_ptr<Expr> funcExpression();

    std::unique_ptr<Expr> yieldExpression();

    std::unique_ptr<Expr> finishCall(
        std::unique_ptr<Expr> callee
    );

    std::unique_ptr<Stmt> statement();

    std::unique_ptr<Stmt> letStatement();
    std::unique_ptr<Stmt> constStatement();

    std::unique_ptr<BlockStmt> blockStatement();

    std::unique_ptr<Stmt> returnStatement();

    std::unique_ptr<Stmt> ifStatement();

    std::unique_ptr<Stmt> whileStatement();

    std::unique_ptr<Stmt> forStatement();
    std::unique_ptr<Stmt> cForStatement(Token forToken);

    std::unique_ptr<Stmt> funcStatement();

    std::unique_ptr<Stmt> objStatement();

    std::unique_ptr<Stmt> breakStatement();

    std::unique_ptr<Stmt> continueStatement();

    std::unique_ptr<Stmt> tryStatement();

    std::unique_ptr<Stmt> throwStatement();

    std::unique_ptr<Stmt> importStatement();
    std::unique_ptr<Stmt> fromImportStatement();
    std::unique_ptr<Stmt> exportStatement();

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
