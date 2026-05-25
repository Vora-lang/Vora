#pragma once

#include <memory>
#include <string>

#include "../lexer/token.h"

namespace vora {

class Expr {
public:
    virtual ~Expr() = default;
};

class LiteralExpr : public Expr {
public:
    explicit LiteralExpr(std::string value)
        : value(std::move(value)) {
    }

    std::string value;
};

class BinaryExpr : public Expr {
public:
    BinaryExpr(
        std::unique_ptr<Expr> left,
        Token op,
        std::unique_ptr<Expr> right
    )
        : left(std::move(left)),
          op(std::move(op)),
          right(std::move(right)) {
    }

    std::unique_ptr<Expr> left;

    Token op;

    std::unique_ptr<Expr> right;
};

}
