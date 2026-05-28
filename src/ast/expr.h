#pragma once

#include <memory>
#include <string>
#include <vector>

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

class VariableExpr : public Expr {
public:
    VariableExpr(
        std::string name,
        Token nameToken
    )
        : name(std::move(name)),
          nameToken(std::move(nameToken)) {
    }

    std::string name;

    Token nameToken;
};

class AssignmentExpr : public Expr {
public:
    AssignmentExpr(
        std::string name,
        std::unique_ptr<Expr> value,
        Token nameToken
    )
        : name(std::move(name)),
          value(std::move(value)),
          nameToken(std::move(nameToken)) {
    }

    std::string name;

    std::unique_ptr<Expr> value;

    Token nameToken;
};

class CallExpr : public Expr {
public:
    CallExpr(
        std::unique_ptr<Expr> callee,
        std::vector<std::unique_ptr<Expr>> arguments,
        Token paren
    )
        : callee(std::move(callee)),
          arguments(std::move(arguments)),
          paren(std::move(paren)) {
    }

    std::unique_ptr<Expr> callee;

    std::vector<std::unique_ptr<Expr>> arguments;

    Token paren;
};

class ArrayExpr : public Expr {
public:
    ArrayExpr(
        std::vector<std::unique_ptr<Expr>> elements,
        Token leftBracket
    )
        : elements(std::move(elements)),
          leftBracket(std::move(leftBracket)) {
    }

    std::vector<std::unique_ptr<Expr>> elements;

    Token leftBracket;
};

class IndexExpr : public Expr {
public:
    IndexExpr(
        std::unique_ptr<Expr> array,
        std::unique_ptr<Expr> index,
        Token bracket
    )
        : array(std::move(array)),
          index(std::move(index)),
          bracket(std::move(bracket)) {
    }

    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    Token bracket;
};

}
