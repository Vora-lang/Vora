#pragma once

#include <memory>
#include <string>

#include "../lexer/token.h"
#include "../runtime/value.h"

namespace vora {

class Expr {
public:
    virtual ~Expr() = default;
};

class LiteralExpr : public Expr {
public:

    explicit LiteralExpr(
        Value value
    )
        : value(std::move(value)) {
    }

    Value value;
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

class GroupingExpr : public Expr {
public:
    explicit GroupingExpr(
        std::unique_ptr<Expr> expression
    )
        : expression(std::move(expression)) {
    }

    std::unique_ptr<Expr> expression;
};

class UnaryExpr : public Expr {
public:
    UnaryExpr(
        Token op,
        std::unique_ptr<Expr> right
    )
        : op(std::move(op)),
          right(std::move(right)) {
    }

    Token op;

    std::unique_ptr<Expr> right;
};

class IdentifierExpr : public Expr {
    public:
        explicit IdentifierExpr(std::string name)
            : name(std::move(name)) {
        }
        std::string name;
};

class AssignmentExpr : public Expr {
public:
    AssignmentExpr(
        std::string name,
        std::unique_ptr<Expr> value
    )
        : name(std::move(name)),
          value(std::move(value)) {
    }

    std::string name;

    std::unique_ptr<Expr> value;
};

}
