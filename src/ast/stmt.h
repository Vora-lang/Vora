#pragma once

#include <memory>
#include <string>
#include <vector>

#include "expr.h"

namespace vora {

class StmtVisitor;

class Stmt {
public:
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& visitor) const = 0;
};

class ExprStmt : public Stmt {
public:
    explicit ExprStmt(
        std::unique_ptr<Expr> expression
    )
        : expression(std::move(expression)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::unique_ptr<Expr> expression;
};

class LetStmt : public Stmt {
public:
    LetStmt(
        std::string name,
        std::unique_ptr<Expr> initializer,
        std::string typeAnnotation = ""
    )
        : name(std::move(name)),
          initializer(std::move(initializer)),
          typeAnnotation(std::move(typeAnnotation)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::string name;

    std::unique_ptr<Expr> initializer;

    std::string typeAnnotation;  // empty = no annotation
};

class BlockStmt : public Stmt {
public:
    explicit BlockStmt(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::vector<std::unique_ptr<Stmt>> statements;
};

class ReturnStmt : public Stmt {
public:
    explicit ReturnStmt(
        std::unique_ptr<Expr> value
    )
        : value(std::move(value)) {
    }

    void accept(StmtVisitor& visitor) const override;

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

    void accept(StmtVisitor& visitor) const override;

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

    void accept(StmtVisitor& visitor) const override;

    std::unique_ptr<Expr> condition;

    std::unique_ptr<Stmt> body;
};

class ForStmt : public Stmt {
public:
    ForStmt(
        std::string variable,
        std::unique_ptr<Expr> iterable,
        std::unique_ptr<Stmt> body,
        Token forToken
    )
        : variable(std::move(variable)),
          iterable(std::move(iterable)),
          body(std::move(body)),
          forToken(std::move(forToken)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::string variable;

    std::unique_ptr<Expr> iterable;

    std::unique_ptr<Stmt> body;

    Token forToken;
};

class FuncStmt : public Stmt {
public:
    FuncStmt(
        std::string name,
        std::vector<std::string> params,
        std::shared_ptr<BlockStmt> body
    )
        : name(std::move(name)),
          params(std::move(params)),
          body(std::move(body)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::string name;

    std::vector<std::string> params;

    std::shared_ptr<BlockStmt> body;
};

class ObjStmt : public Stmt {
public:
    ObjStmt(
        std::string name,
        std::string parentName,
        std::vector<std::string> params,
        std::vector<std::unique_ptr<Stmt>> methods,
        std::shared_ptr<BlockStmt> body
    )
        : name(std::move(name)),
          parentName(std::move(parentName)),
          params(std::move(params)),
          methods(std::move(methods)),
          body(std::move(body)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::string name;

    std::string parentName;  // empty = no inheritance

    std::vector<std::string> params;

    std::vector<std::unique_ptr<Stmt>> methods;

    std::shared_ptr<BlockStmt> body;
};

class BreakStmt : public Stmt {
public:
    explicit BreakStmt(Token keyword)
        : keyword(std::move(keyword)) {
    }

    void accept(StmtVisitor& visitor) const override;

    Token keyword;
};

class ContinueStmt : public Stmt {
public:
    explicit ContinueStmt(Token keyword)
        : keyword(std::move(keyword)) {
    }

    void accept(StmtVisitor& visitor) const override;

    Token keyword;
};

class ThrowStmt : public Stmt {
public:
    explicit ThrowStmt(
        std::unique_ptr<Expr> value,
        Token keyword
    )
        : value(std::move(value)),
          keyword(std::move(keyword)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::unique_ptr<Expr> value;
    Token keyword;
};

class TryStmt : public Stmt {
public:
    TryStmt(
        std::unique_ptr<Stmt> tryBlock,
        std::string catchVar,
        std::unique_ptr<Stmt> catchBlock,
        std::unique_ptr<Stmt> finallyBlock
    )
        : tryBlock(std::move(tryBlock)),
          catchVar(std::move(catchVar)),
          catchBlock(std::move(catchBlock)),
          finallyBlock(std::move(finallyBlock)) {
    }

    void accept(StmtVisitor& visitor) const override;

    std::unique_ptr<Stmt> tryBlock;
    std::string catchVar;               // empty = no catch clause
    std::unique_ptr<Stmt> catchBlock;   // nullptr if no catch
    std::unique_ptr<Stmt> finallyBlock; // nullptr if no finally
};

}
