#include "parser.h"

namespace vora {

Parser::Parser(std::vector<Token> tokens)
    : tokens(std::move(tokens)) {
}

std::unique_ptr<Expr> Parser::parse() {
    return expression();
}

std::unique_ptr<Expr> Parser::expression() {
    return parsePrecedence(0);
}

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
    if (peek().type != type) {
        return false;
    }

    advance();

    return true;
}

int Parser::getPrecedence(TokenType type) const {
    switch (type) {

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

std::unique_ptr<Expr> Parser::primary() {

    if (match(TokenType::NUMBER)) {
        return std::make_unique<LiteralExpr>(
            previous().lexeme
        );
    }

    return nullptr;
}

std::unique_ptr<Expr> Parser::parsePrecedence(int precedence) {

    auto left = primary();

    while (!isAtEnd() &&
           precedence < getPrecedence(peek().type)) {

        Token op = advance();

        int newPrecedence = getPrecedence(op.type);

        if (op.type == TokenType::POWER) {
            newPrecedence--;
        }

        auto right = parsePrecedence(newPrecedence);

        left = std::make_unique<BinaryExpr>(
            std::move(left),
            op,
            std::move(right)
        );
    }

    return left;
}

}
