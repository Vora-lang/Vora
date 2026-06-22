#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../lexer/token.h"
#include "../runtime/value.h"
#include "param_decl.h"
#include "binding_pattern.h"

namespace vora {

template <typename R>
class ExprVisitor;

class BlockStmt;  // forward decl — FuncExpr uses shared_ptr (works with incomplete type)

class Expr {
public:
    virtual ~Expr() = default;

    // One accept() overload per return type in use. When adding a new pass
    // with a new R, add a new pure-virtual overload here and implement it
    // in each concrete subclass (expr.cpp).
    virtual Value        accept(ExprVisitor<Value>& visitor)        const = 0;
    virtual void         accept(ExprVisitor<void>& visitor)         const = 0;
    virtual std::string  accept(ExprVisitor<std::string>& visitor)  const = 0;

    virtual std::unique_ptr<Expr> clone() const = 0;
};

class LiteralExpr : public Expr {
public:

    explicit LiteralExpr(
        Value value
    )
        : value(std::move(value)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::string name;

    std::unique_ptr<Expr> value;

    Token nameToken;
};

// CompoundAssignmentExpr — x += y, obj.prop -= z, arr[i] *= 3
// Stored as a first-class AST node (not desugared) so the formatter can
// reproduce the original syntax. The compiler desugars it internally.
class CompoundAssignmentExpr : public Expr {
public:
    CompoundAssignmentExpr(
        std::unique_ptr<Expr> target,
        Token op,
        std::unique_ptr<Expr> value
    )
        : target(std::move(target)),
          op(std::move(op)),
          value(std::move(value)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> target;  // VariableExpr, PropertyExpr, or IndexExpr
    Token op;                        // +=, -=, *=, /=, %=
    std::unique_ptr<Expr> value;    // right-hand side
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::vector<std::unique_ptr<Expr>> elements;

    Token leftBracket;
};

class DictExpr : public Expr {
public:
    DictExpr(
        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs,
        Token leftBrace
    )
        : pairs(std::move(pairs)),
          leftBrace(std::move(leftBrace)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;
    Token leftBrace;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> object;
    std::string property;
    std::unique_ptr<Expr> value;
    Token dot;
};

class IndexAssignmentExpr : public Expr {
public:
    IndexAssignmentExpr(
        std::unique_ptr<Expr> object,
        std::unique_ptr<Expr> index,
        std::unique_ptr<Expr> value,
        Token bracket
    )
        : object(std::move(object)),
          index(std::move(index)),
          value(std::move(value)),
          bracket(std::move(bracket)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
    Token bracket;
};

class ThisExpr : public Expr {
public:
    explicit ThisExpr(
        Token keyword
    )
        : keyword(std::move(keyword)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Token keyword;
};

class SuperExpr : public Expr {
public:
    explicit SuperExpr(
        Token keyword
    )
        : keyword(std::move(keyword)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
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

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> thenBranch;
    std::unique_ptr<Expr> elseBranch;
};

// Match expression:  match <scrutinee> { <pattern> => <body>, ... }
// Produces a value (expression context). Can also appear as an expression
// statement where the value is discarded.
// Patterns are simple tagged unions (not polymorphic) — see MatchPattern.

struct BlockStmt;

enum class PatternKind : uint8_t { Literal, Wildcard, Range };

struct MatchPattern {
    PatternKind kind = PatternKind::Wildcard;
    Value literal;         // Kind::Literal
    Value rangeLow;        // Kind::Range
    Value rangeHigh;       // Kind::Range
    bool rangeInclusive = true;  // true = ..=, false = ..
    Token start;           // First token of the pattern (for error reporting)
};

struct MatchCase {
    std::vector<MatchPattern> patterns;   // at least 1
    std::unique_ptr<Expr> body;           // expression body (=> expr)
    std::shared_ptr<BlockStmt> blockBody; // block body (=> { ... })
    Token arrow;                          // => token
};

class MatchExpr : public Expr {
public:
    MatchExpr(
        std::unique_ptr<Expr> scrutinee,
        std::vector<MatchCase> cases,
        Token matchKeyword,
        Token rightBrace
    )
        : scrutinee(std::move(scrutinee)),
          cases(std::move(cases)),
          matchKeyword(std::move(matchKeyword)),
          rightBrace(std::move(rightBrace)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchCase> cases;
    Token matchKeyword;
    Token rightBrace;
};

// Anonymous function expression:  func(x, y) { body }
// In expression contexts, `func` without a name creates a lambda.
// Body uses shared_ptr<BlockStmt> — shared_ptr works with incomplete types,
// avoiding the circular dependency between expr.h and stmt.h.
class FuncExpr : public Expr {
public:
    FuncExpr(
        std::vector<ParamDecl> params,
        std::shared_ptr<BlockStmt> body
    )
        : params(std::move(params)),
          body(std::move(body)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::vector<ParamDecl> params;
    std::shared_ptr<BlockStmt> body;
};

// yield <expr>  — generator suspension expression
class YieldExpr : public Expr {
public:
    YieldExpr(std::unique_ptr<Expr> value, Token keyword)
        : value(std::move(value)), keyword(std::move(keyword)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> value;  // nullptr = yield with no value (yields null)
    Token keyword;
};

// DestructureAssignmentExpr — bare destructuring assignment (without let/const).
// [a, b] = arr   or   {x, y} = obj
// Pushes the assigned value onto the stack (assignment is an expression in Vora).
class DestructureAssignmentExpr : public Expr {
public:
    DestructureAssignmentExpr(
        std::unique_ptr<BindingPattern> binding,
        std::unique_ptr<Expr> value
    )
        : binding(std::move(binding)),
          value(std::move(value)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<BindingPattern> binding;
    std::unique_ptr<Expr> value;
};

// ErrorExpr — placeholder for a parse error in expression context.
// Used by error-tolerant parsing to retain partial AST structure.
// Has no meaningful value; visitors that execute code should treat it as null.
class ErrorExpr : public Expr {
public:
    ErrorExpr(std::string message, Token errorToken)
        : message(std::move(message)), errorToken(std::move(errorToken)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::string message;
    Token errorToken;
};

// OptionalChainExpr — optional property/call/index access via ?.
// a?.b, a?.(), a?.[i]
// If the object is null, the entire expression short-circuits to null.
// Otherwise, the operation proceeds normally.
class OptionalChainExpr : public Expr {
public:
    enum class Kind : uint8_t { PROPERTY, CALL, INDEX };

    OptionalChainExpr(
        std::unique_ptr<Expr> object,
        Kind kind,
        Token questionDot
    )
        : object(std::move(object)),
          kind(kind),
          questionDot(std::move(questionDot)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> object;
    Kind kind;
    Token questionDot;   // the ?. token

    // Kind::PROPERTY
    std::string property;

    // Kind::CALL
    std::vector<std::unique_ptr<Expr>> arguments;
    Token closeParen;    // )

    // Kind::INDEX
    std::unique_ptr<Expr> index;
    Token closeBracket;  // ]
};

}
