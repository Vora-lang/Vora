#pragma once

#include <memory>
#include <string>
#include <vector>

#include "expr.h"
#include "param_decl.h"

namespace vora {

template <typename R>
class StmtVisitor;

class Stmt {
public:
    virtual ~Stmt() = default;

    // One accept() overload per return type in use. When adding a new pass
    // with a new R, add a new pure-virtual overload here and implement it
    // in each concrete subclass (stmt.cpp).
    virtual void         accept(StmtVisitor<void>& visitor)         const = 0;
    virtual std::string  accept(StmtVisitor<std::string>& visitor)  const = 0;
};

class ExprStmt : public Stmt {
public:
    explicit ExprStmt(
        std::unique_ptr<Expr> expression
    )
        : expression(std::move(expression)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::unique_ptr<Expr> expression;
};

class LetStmt : public Stmt {
public:
    LetStmt(
        std::string name,
        Token nameToken,
        std::unique_ptr<Expr> initializer,
        std::string typeAnnotation = "",
        bool isConst = false
    )
        : name(std::move(name)),
          nameToken(std::move(nameToken)),
          initializer(std::move(initializer)),
          typeAnnotation(std::move(typeAnnotation)),
          isConst(isConst) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string name;

    Token nameToken;  // position of the variable name

    std::unique_ptr<Expr> initializer;

    std::string typeAnnotation;  // empty = no annotation

    bool isConst = false;  // true if declared with 'const' keyword
};

class BlockStmt : public Stmt {
public:
    explicit BlockStmt(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::vector<std::unique_ptr<Stmt>> statements;
};

class ReturnStmt : public Stmt {
public:
    explicit ReturnStmt(
        std::unique_ptr<Expr> value
    )
        : value(std::move(value)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

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

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

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

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

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

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string variable;

    std::unique_ptr<Expr> iterable;

    std::unique_ptr<Stmt> body;

    Token forToken;
};

// C-style for loop: for (initializer; condition; increment) body
// - initializer: let stmt, expression stmt, or null (e.g. `for (;;)`)
// - condition:   expression or null (null = always true)
// - increment:   expression or null
class CForStmt : public Stmt {
public:
    CForStmt(
        std::unique_ptr<Stmt> initializer,
        std::unique_ptr<Expr> condition,
        std::unique_ptr<Expr> increment,
        std::unique_ptr<Stmt> body
    )
        : initializer(std::move(initializer)),
          condition(std::move(condition)),
          increment(std::move(increment)),
          body(std::move(body)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::unique_ptr<Stmt> initializer;  // let stmt, expression stmt, or nullptr
    std::unique_ptr<Expr> condition;    // nullptr = always true
    std::unique_ptr<Expr> increment;    // nullptr = no increment
    std::unique_ptr<Stmt> body;
};

class FuncStmt : public Stmt {
public:
    FuncStmt(
        std::string name,
        Token nameToken,
        std::vector<ParamDecl> params,
        std::shared_ptr<BlockStmt> body
    )
        : name(std::move(name)),
          nameToken(std::move(nameToken)),
          params(std::move(params)),
          body(std::move(body)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string name;

    Token nameToken;  // position of the function name

    std::vector<ParamDecl> params;

    std::shared_ptr<BlockStmt> body;
};

class ObjStmt : public Stmt {
public:
    ObjStmt(
        std::string name,
        Token nameToken,
        std::vector<std::string> parentNames,
        std::vector<ParamDecl> params,
        std::vector<std::unique_ptr<Stmt>> methods,
        std::shared_ptr<BlockStmt> body
    )
        : name(std::move(name)),
          nameToken(std::move(nameToken)),
          parentNames(std::move(parentNames)),
          params(std::move(params)),
          methods(std::move(methods)),
          body(std::move(body)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string name;

    Token nameToken;  // position of the class name

    std::vector<std::string> parentNames;  // empty = no inheritance

    std::vector<ParamDecl> params;

    std::vector<std::unique_ptr<Stmt>> methods;

    std::shared_ptr<BlockStmt> body;
};

class BreakStmt : public Stmt {
public:
    explicit BreakStmt(Token keyword)
        : keyword(std::move(keyword)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    Token keyword;
};

class ContinueStmt : public Stmt {
public:
    explicit ContinueStmt(Token keyword)
        : keyword(std::move(keyword)) {
    }

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

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

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

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

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::unique_ptr<Stmt> tryBlock;
    std::string catchVar;               // empty = no catch clause
    std::unique_ptr<Stmt> catchBlock;   // nullptr if no catch
    std::unique_ptr<Stmt> finallyBlock; // nullptr if no finally
};

// import "path" — statement that auto-binds a module variable (Python 3 style).
// The variable name is derived from the path basename (without .va extension),
// or from the optional `as` alias: `import "math" as M` binds variable `M`.
//
// from "path" import a, b, c — imports specific names directly into scope.
// When importNames is non-empty, no module variable is created; each name
// in importNames is extracted from the module dict and bound individually.
class ImportStmt : public Stmt {
public:
    ImportStmt(std::string modulePath, Token keyword, std::string alias = "",
               std::vector<std::string> importNames = {})
        : modulePath(std::move(modulePath)), keyword(std::move(keyword)),
          alias(std::move(alias)), importNames(std::move(importNames)) {}

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string modulePath;    // raw path string from source
    Token keyword;             // 'import' or 'from' token (for error location)
    std::string alias;         // optional 'as' alias name (empty = derive from basename)
    std::vector<std::string> importNames;  // non-empty = "from...import" form
};

// export <declaration> — marks a declaration as publicly visible.
// Wraps a FuncStmt, LetStmt, ConstStmt, or ObjStmt.
class ExportStmt : public Stmt {
public:
    ExportStmt(std::unique_ptr<Stmt> declaration, Token keyword)
        : declaration(std::move(declaration)), keyword(std::move(keyword)) {}

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::unique_ptr<Stmt> declaration;  // FuncStmt / LetStmt / ObjStmt (const uses LetStmt.isConst)
    Token keyword;                       // 'export' token
};

// ErrorStmt — placeholder for a parse error in statement context.
// Used by error-tolerant parsing to retain partial AST structure.
// Visitors that execute code should treat it as a no-op.
class ErrorStmt : public Stmt {
public:
    ErrorStmt(std::string message, Token errorToken)
        : message(std::move(message)), errorToken(std::move(errorToken)) {}

    void        accept(StmtVisitor<void>& visitor)        const override;
    std::string accept(StmtVisitor<std::string>& visitor) const override;

    std::string message;
    Token errorToken;
};

}
