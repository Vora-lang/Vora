#pragma once

#include <memory>
#include <string>
#include <vector>

#include "expr.h"

namespace vora {

class Stmt {
public:
    virtual ~Stmt() = default;
};

class ExprStmt : public Stmt {
public:
    explicit ExprStmt(
        std::unique_ptr<Expr> expression
    )
        : expression(std::move(expression)) {
    }

    std::unique_ptr<Expr> expression;
};

class LetStmt : public Stmt {
public:
    LetStmt(
        std::string name,
        std::unique_ptr<Expr> initializer
    )
        : name(std::move(name)),
          initializer(std::move(initializer)) {
    }

    std::string name;

    std::unique_ptr<Expr> initializer;
};

class BlockStmt : public Stmt {
public:
    explicit BlockStmt(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    std::vector<std::unique_ptr<Stmt>> statements;
};

class ReturnStmt : public Stmt {
public:
    explicit ReturnStmt(
        std::unique_ptr<Expr> value
    )
        : value(std::move(value)) {
    }

    std::unique_ptr<Expr> value;
};

class IfStmt : public Stmt {
public:
    IfStmt(
        std::unique_ptr<Expr> condition,
        std::unique_ptr<Stmt> thenBranch,
        std::unique_ptr<Stmt> elseBranch
    )
        : condition(std::move(condition)),
          thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {
    }

    std::unique_ptr<Expr> condition;

    std::unique_ptr<Stmt> thenBranch;

    std::unique_ptr<Stmt> elseBranch;
};

class WhileStmt : public Stmt {
public:
    WhileStmt(
        std::unique_ptr<Expr> condition,
        std::unique_ptr<Stmt> body
    )
        : condition(std::move(condition)),
          body(std::move(body)) {
    }

    std::unique_ptr<Expr> condition;

    std::unique_ptr<Stmt> body;
};

}
