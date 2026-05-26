#include "parser.h"
#include "../ast/stmt.h"
#include <iostream>

namespace vora {

Parser::Parser(std::vector<Token> tokens)
    : tokens(std::move(tokens)) {
}

std::unique_ptr<Program> Parser::parse() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd()) {

        auto stmt = statement();

        if (!stmt) {
            return nullptr;
        }

        statements.push_back(
            std::move(stmt)
        );
    }

    return std::make_unique<Program>(
        std::move(statements)
    );
}

// =========================
// ENTRY
// =========================

std::unique_ptr<Expr> Parser::expression() {
    // Pratt root: assignment is handled inside Pratt
    return parsePrecedence(0);
}

std::unique_ptr<Stmt> Parser::statement() {

    if (match(TokenType::LET)) {
        return letStatement();
    }

    if (match(TokenType::LEFT_BRACE)) {
        return blockStatement();
    }

    if (match(TokenType::RETURN)) {
        return returnStatement();
    }

    if (match(TokenType::IF)) {
        return ifStatement();
    }

    if (match(TokenType::WHILE)) {
        return whileStatement();
    }

    auto expr = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<ExprStmt>(
        std::move(expr)
    );
}

std::unique_ptr<Stmt> Parser::letStatement() {

    if (!match(TokenType::IDENTIFIER)) {

        std::cerr
            << "Expected variable name\n";

        return nullptr;
    }

    std::string name = previous().lexeme;

    if (!match(TokenType::EQUAL)) {

        std::cerr
            << "Expected '=' after variable name\n";

        return nullptr;
    }

    auto initializer = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<LetStmt>(
        name,
        std::move(initializer)
    );
}

std::unique_ptr<Stmt> Parser::blockStatement() {

    std::vector<std::unique_ptr<Stmt>> statements;

    while (!isAtEnd() &&
           peek().type != TokenType::RIGHT_BRACE) {

        auto stmt = statement();

        if (!stmt) {
            return nullptr;
        }

        statements.push_back(
            std::move(stmt)
        );
    }

    if (!match(TokenType::RIGHT_BRACE)) {

        std::cerr
            << "Expected '}' after block\n";

        return nullptr;
    }

    return std::make_unique<BlockStmt>(
        std::move(statements)
    );
}

std::unique_ptr<Stmt> Parser::returnStatement() {

    auto value = expression();

    match(TokenType::SEMICOLON);

    return std::make_unique<ReturnStmt>(
        std::move(value)
    );
}

std::unique_ptr<Stmt> Parser::ifStatement() {

    if (!match(TokenType::LEFT_PAREN)) {

        std::cerr
            << "Expected '(' after if\n";

        return nullptr;
    }

    auto condition = expression();

    if (!match(TokenType::RIGHT_PAREN)) {

        std::cerr
            << "Expected ')' after condition\n";

        return nullptr;
    }

    auto thenBranch = statement();

    std::unique_ptr<Stmt> elseBranch;

    if (match(TokenType::ELSE)) {
        elseBranch = statement();
    }

    return std::make_unique<IfStmt>(
        std::move(condition),
        std::move(thenBranch),
        std::move(elseBranch)
    );
}

std::unique_ptr<Stmt> Parser::whileStatement() {

    if (!match(TokenType::LEFT_PAREN)) {

        std::cerr
            << "Expected '(' after while\n";

        return nullptr;
    }

    auto condition = expression();

    if (!match(TokenType::RIGHT_PAREN)) {

        std::cerr
            << "Expected ')' after condition\n";

        return nullptr;
    }

    auto body = statement();

    return std::make_unique<WhileStmt>(
        std::move(condition),
        std::move(body)
    );
}

// =========================
// UTIL
// =========================

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

Token Parser::peek() const {
    return tokens[current];
}

Token Parser::previous() const {
    return tokens[current - 1];
}

Token Parser::advance() {
    if (!isAtEnd()) {
        current++;
    }
    return previous();
}

bool Parser::match(TokenType type) {
    if (peek().type != type) return false;
    advance();
    return true;
}

// =========================
// PRECEDENCE TABLE
// =========================

int Parser::getPrecedence(TokenType type) const {
    switch (type) {

        case TokenType::EQUAL:
            return 1; // lowest, right-associative handled in Pratt

        case TokenType::OR:
            return 1;

        case TokenType::AND:
            return 2;

        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
            return 3;

        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL:
            return 4;

        case TokenType::PLUS:
        case TokenType::MINUS:
            return 5;

        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
            return 6;

        case TokenType::POWER:
            return 7;

        default:
            return 0;
    }
}

// =========================
// PRIMARY
// =========================

std::unique_ptr<Expr> Parser::primary() {

    if (match(TokenType::MINUS)) {
        Token op = previous();
        auto right = primary();

        if (!right) return nullptr;

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    if (match(TokenType::NOT)) {
        Token op = previous();
        auto right = primary();

        if (!right) return nullptr;

        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    if (match(TokenType::NUMBER)) {
        return std::make_unique<LiteralExpr>(previous().lexeme);
    }

    if (match(TokenType::IDENTIFIER)) {
        return std::make_unique<IdentifierExpr>(previous().lexeme);
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();

        if (!match(TokenType::RIGHT_PAREN)) {
            std::cerr << "Expected ')'\n";
            return nullptr;
        }

        return std::make_unique<GroupingExpr>(std::move(expr));
    }

    std::cerr << "Unexpected token: " << peek().lexeme << "\n";
    return nullptr;
}

// =========================
// PRATT CORE
// =========================

std::unique_ptr<Expr> Parser::parsePrecedence(int precedence) {

    auto left = primary();

    if (!left) {
        std::cerr << "Expected expression\n";
        return nullptr;
    }

    while (!isAtEnd() &&
           precedence < getPrecedence(peek().type)) {

        Token op = advance();
        int opPrec = getPrecedence(op.type);

        // right-associativity fix (power + assignment)
        bool rightAssociative =
            (op.type == TokenType::POWER ||
             op.type == TokenType::EQUAL);

        int nextPrec = opPrec - (rightAssociative ? 1 : 0);

        auto right = parsePrecedence(nextPrec);

        if (!right) {
            std::cerr << "Expected right-hand expression after operator\n";
            return nullptr;
        }

        // =========================
        // ASSIGNMENT (Pratt unified)
        // =========================
        if (op.type == TokenType::EQUAL) {

            auto identifier =
                dynamic_cast<IdentifierExpr*>(left.get());

            if (!identifier) {
                std::cerr << "Invalid assignment target\n";
                return nullptr;
            }

            return std::make_unique<AssignmentExpr>(
                identifier->name,
                std::move(right)
            );
        }

        // =========================
        // NORMAL BINARY
        // =========================
        left = std::make_unique<BinaryExpr>(
            std::move(left),
            op,
            std::move(right)
        );
    }

    return left;
}

}
