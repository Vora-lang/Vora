#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../lexer/token.h"
#include "../runtime/value.h"

namespace vora {

class ExprVisitor;

class Expr {
public:
    virtual ~Expr() = default;
    virtual Value accept(ExprVisitor& visitor) const = 0;
    virtual std::unique_ptr<Expr> clone() const = 0;
};

class LiteralExpr : public Expr {
public:

    explicit LiteralExpr(
        Value value
    )
        : value(std::move(value)) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

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

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    Token bracket;
};

class PropertyExpr : public Expr {
public:
    PropertyExpr(
        std::unique_ptr<Expr> object,
        std::string property,
        Token dot
    )
        : object(std::move(object)),
          property(std::move(property)),
          dot(std::move(dot)) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> object;
    std::string property;
    Token dot;
};

class PropertyAssignmentExpr : public Expr {
public:
    PropertyAssignmentExpr(
        std::unique_ptr<Expr> object,
        std::string property,
        std::unique_ptr<Expr> value,
        Token dot
    )
        : object(std::move(object)),
          property(std::move(property)),
          value(std::move(value)),
          dot(std::move(dot)) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> object;
    std::string property;
    std::unique_ptr<Expr> value;
    Token dot;
};

class ThisExpr : public Expr {
public:
    explicit ThisExpr(
        Token keyword
    )
        : keyword(std::move(keyword)) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Token keyword;
};

class IncDecExpr : public Expr {
public:
    IncDecExpr(
        Token op,
        std::unique_ptr<Expr> target,
        bool isPrefix
    )
        : op(std::move(op)),
          target(std::move(target)),
          isPrefix(isPrefix) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Token op;           // PLUS_PLUS or MINUS_MINUS
    std::unique_ptr<Expr> target;  // VariableExpr or PropertyExpr
    bool isPrefix;      // true = ++x, false = x++
};

class TernaryExpr : public Expr {
public:
    TernaryExpr(
        std::unique_ptr<Expr> condition,
        std::unique_ptr<Expr> thenBranch,
        std::unique_ptr<Expr> elseBranch
    )
        : condition(std::move(condition)),
          thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {
    }

    Value accept(ExprVisitor& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> thenBranch;
    std::unique_ptr<Expr> elseBranch;
};

}
