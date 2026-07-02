/**
 * @file expr.h
 * @brief Expression AST nodes and supporting types for the Vora language.
 *
 * Defines the abstract base class Expr and all concrete expression node types
 * used in the Vora AST. Each expression node inherits from Expr and implements
 * the templated ExprVisitor<R> dispatch mechanism via three accept() overloads
 * (Value, void, std::string).
 *
 * Also defines the supporting types for pattern matching (PatternKind,
 * MatchPattern, MatchCase) and the ExprVisitor<R> forward declaration.
 *
 * The expression hierarchy:
 *   Expr (abstract base)
 *   +-- LiteralExpr             — literal values (numbers, strings, bool, null)
 *   +-- BinaryExpr              — binary operators (+, -, *, /, ==, &&, etc.)
 *   +-- GroupingExpr            — parenthesized sub-expression
 *   +-- UnaryExpr               — unary operators (!, -, ~)
 *   +-- VariableExpr            — variable name reference
 *   +-- AssignmentExpr          — simple variable assignment (x = val)
 *   +-- CompoundAssignmentExpr  — compound assignment (x += val, etc.)
 *   +-- CallExpr                — function/method call
 *   +-- ArrayExpr               — array literal [...]
 *   +-- DictExpr                — dict literal {...}
 *   +-- IndexExpr               — indexed access arr[i]
 *   +-- PropertyExpr            — property access obj.prop
 *   +-- PropertyAssignmentExpr  — property set obj.prop = val
 *   +-- IndexAssignmentExpr     — indexed set arr[i] = val
 *   +-- ThisExpr                — 'this' keyword reference
 *   +-- SuperExpr               — 'super' keyword reference
 *   +-- IncDecExpr              — increment/decrement (++x, x--)
 *   +-- TernaryExpr             — ternary conditional (cond ? a : b)
 *   +-- MatchExpr               — match expression (match x { ... })
 *   +-- FuncExpr                — anonymous function expression (lambda)
 *   +-- YieldExpr               — generator yield
 *   +-- DestructureAssignmentExpr — bare destructuring assignment
 *   +-- SpreadExpr              — call-site spread (...expr)
 *   +-- ListCompExpr            — list comprehension [expr for ...]
 *   +-- DictCompExpr            — dict comprehension {k:v for ...}
 *   +-- ErrorExpr               — placeholder for parse errors
 *   +-- OptionalChainExpr       — optional chaining (?.)
 *
 * @see vora::Stmt
 * @see vora::Program
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../lexer/token.h"
#include "../runtime/value.h"
#include "param_decl.h"
#include "binding_pattern.h"

namespace vora {

/// @brief Visitor interface for traversing expression AST nodes.
///
/// ExprVisitor<R> is templated on the return type R so that a single
/// interface definition serves all passes (e.g. Value for constant folding,
/// void for side-effectful compilation, std::string for pretty-printing).
///
/// @tparam R  The return type of the visit methods.
template <typename R>
class ExprVisitor;

/// @brief Forward declaration of BlockStmt.
///
/// FuncExpr uses shared_ptr<BlockStmt> for its body, which works with
/// incomplete types and avoids the circular dependency between expr.h and stmt.h.
class BlockStmt;  // forward decl — FuncExpr uses shared_ptr (works with incomplete type)

/**
 * @brief Abstract base class for all expression AST nodes.
 *
 * Every concrete expression type inherits from Expr and implements the visitor
 * dispatch via three accept() overloads (Value, void, std::string).
 * When adding a new visitor pass with a new return type R, add a new
 * pure-virtual accept() overload here and implement it in each subclass.
 */
class Expr {
public:
    virtual ~Expr() = default;

    // One accept() overload per return type in use. When adding a new pass
    // with a new R, add a new pure-virtual overload here and implement it
    // in each concrete subclass (expr.cpp).
    /// @brief Accept a Value-returning visitor (used for constant folding/evaluation).
    virtual Value        accept(ExprVisitor<Value>& visitor)        const = 0;
    /// @brief Accept a void-returning visitor (used for compilation).
    virtual void         accept(ExprVisitor<void>& visitor)         const = 0;
    /// @brief Accept a string-returning visitor (used for AST pretty-printing).
    virtual std::string  accept(ExprVisitor<std::string>& visitor)  const = 0;

    /// @brief Deep-clone this expression node.
    /// @return A unique_ptr to an independent copy of the expression tree.
    virtual std::unique_ptr<Expr> clone() const = 0;
};

/**
 * @brief A literal value expression (numbers, strings, booleans, null).
 *
 * Wraps a single runtime Value (variant type) representing a compile-time
 * constant. No evaluation is needed — the compiler emits OP_CONSTANT.
 */
class LiteralExpr : public Expr {
public:
    /// @brief Construct a LiteralExpr from a runtime Value.
    /// @param value  The literal value (transferred by move).
    explicit LiteralExpr(
        Value value
    )
        : value(std::move(value)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Value value;  ///< The literal value.
};

/**
 * @brief A binary operator expression (a + b, x && y, etc.).
 *
 * Represents any two-operand operation: arithmetic, comparison, logical,
 * bitwise, and string concatenation. The operator is stored as a Token.
 */
class BinaryExpr : public Expr {
public:
    /// @brief Construct a BinaryExpr.
    /// @param left   The left-hand operand.
    /// @param op     The operator token.
    /// @param right  The right-hand operand.
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

    std::unique_ptr<Expr> left;   ///< Left-hand operand expression.

    Token op;                     ///< The operator token.

    std::unique_ptr<Expr> right;  ///< Right-hand operand expression.
};

/**
 * @brief A parenthesized sub-expression `(expr)`.
 *
 * Groups an inner expression to control operator precedence.
 * The grouping itself has no runtime effect beyond evaluating the inner
 * expression; the compiler simply compiles through to the inner node.
 */
class GroupingExpr : public Expr {
public:
    /// @brief Construct a GroupingExpr.
    /// @param expression  The inner expression.
    explicit GroupingExpr(
        std::unique_ptr<Expr> expression
    )
        : expression(std::move(expression)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> expression;  ///< The inner expression.
};

/**
 * @brief A unary operator expression (!x, -x, ~x).
 *
 * Represents a single-operand operation: logical not, arithmetic negation,
 * or bitwise complement.
 */
class UnaryExpr : public Expr {
public:
    /// @brief Construct a UnaryExpr.
    /// @param op     The operator token (!, -, ~).
    /// @param right  The operand expression.
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

    Token op;                     ///< The operator token (!, -, ~).

    std::unique_ptr<Expr> right;  ///< The operand expression.
};

/**
 * @brief A variable name reference expression.
 *
 * Resolves to the value of a named variable (local or global) at compile
 * time. The compiler emits OP_GET_LOCAL or OP_GET_GLOBAL depending on scope.
 */
class VariableExpr : public Expr {
public:
    /// @brief Construct a VariableExpr.
    /// @param name       The variable name.
    /// @param nameToken  The token for source position tracking.
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

    std::string name;     ///< The variable name.

    Token nameToken;      ///< Source position for diagnostics.
};

/**
 * @brief A simple variable assignment expression (x = value).
 *
 * Assigns a value to a named variable (local or global). The compiler
 * resolves the variable at compile time and emits OP_SET_LOCAL or
 * OP_SET_GLOBAL. For property and index assignments, see
 * PropertyAssignmentExpr and IndexAssignmentExpr.
 */
class AssignmentExpr : public Expr {
public:
    /// @brief Construct an AssignmentExpr.
    /// @param name       The variable name being assigned to.
    /// @param value      The right-hand side value expression.
    /// @param nameToken  Source position of the variable name.
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

    std::string name;              ///< The variable name being assigned to.

    std::unique_ptr<Expr> value;   ///< The right-hand side value expression.

    Token nameToken;               ///< Source position for diagnostics.
};

/**
 * @brief Compound assignment expression (x += y, obj.prop -= z, arr[i] *= 3).
 *
 * Stored as a first-class AST node (not desugared) so the formatter can
 * reproduce the original syntax. The compiler desugars it internally.
 */
// Stored as a first-class AST node (not desugared) so the formatter can
// reproduce the original syntax. The compiler desugars it internally.
class CompoundAssignmentExpr : public Expr {
public:
    /// @brief Construct a CompoundAssignmentExpr.
    /// @param target  The assignment target (VariableExpr, PropertyExpr, or IndexExpr).
    /// @param op      The compound operator token (+=, -=, *=, /=, %=).
    /// @param value   The right-hand side value expression.
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

    std::unique_ptr<Expr> target;  ///< VariableExpr, PropertyExpr, or IndexExpr
    Token op;                        ///< +=, -=, *=, /=, %=
    std::unique_ptr<Expr> value;    ///< right-hand side
};

/**
 * @brief A function or method call expression f(a, b, name: c).
 *
 * Supports positional and named arguments. The callee can be any expression
 * that evaluates to a callable (function name, property expression for
 * methods, or an anonymous function).
 */
class CallExpr : public Expr {
public:
    /// @brief Construct a CallExpr.
    /// @param callee         The expression that evaluates to a callable.
    /// @param arguments      The argument expressions (positional and named mixed).
    /// @param paren          The closing parenthesis token for source position.
    /// @param argumentNames  Parallel to arguments; empty string = positional, non-empty = named arg.
    CallExpr(
        std::unique_ptr<Expr> callee,
        std::vector<std::unique_ptr<Expr>> arguments,
        Token paren,
        std::vector<std::string> argumentNames = {}
    )
        : callee(std::move(callee)),
          arguments(std::move(arguments)),
          paren(std::move(paren)),
          argumentNames(std::move(argumentNames)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> callee;                ///< The callable expression.

    std::vector<std::unique_ptr<Expr>> arguments; ///< Argument expressions.

    Token paren;                                 ///< Closing parenthesis for diagnostics.

    // Parallel to arguments; empty string = positional arg, non-empty = named arg.
    std::vector<std::string> argumentNames;      ///< Argument names (empty = positional).
};

/**
 * @brief An array literal expression `[elem1, elem2, ...]`.
 *
 * Evaluates each element and constructs a new Array value at runtime.
 * Element expressions are evaluated left-to-right.
 */
class ArrayExpr : public Expr {
public:
    /// @brief Construct an ArrayExpr.
    /// @param elements     The element expressions.
    /// @param leftBracket  The '[' token for source position.
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

    std::vector<std::unique_ptr<Expr>> elements;  ///< Element expressions (may be empty).

    Token leftBracket;                             ///< The '[' token for diagnostics.
};

/**
 * @brief A dictionary literal expression `{key: val, key2: val2, ...}`.
 *
 * Evaluates each key-value pair and constructs a new Dict value at runtime.
 * Keys and values are both expressions, evaluated left-to-right per pair.
 */
class DictExpr : public Expr {
public:
    /// @brief Construct a DictExpr.
    /// @param pairs      The key-value pair expressions.
    /// @param leftBrace  The '{' token for source position.
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

    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;  ///< Key-value pairs.

    Token leftBrace;  ///< The '{' token for diagnostics.
};

/**
 * @brief An index access expression `obj[index]`.
 *
 * Evaluates both the object and the index, then retrieves the value at that
 * key. Works on arrays (integer index), dicts (key lookup), and strings.
 */
class IndexExpr : public Expr {
public:
    /// @brief Construct an IndexExpr.
    /// @param array    The expression that evaluates to an indexable value.
    /// @param index    The index/key expression.
    /// @param bracket  The '[' or ']' token for source position.
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

    std::unique_ptr<Expr> array;   ///< The indexable object expression.
    std::unique_ptr<Expr> index;   ///< The index/key expression.
    Token bracket;                 ///< Source position for diagnostics.
};

/**
 * @brief A property access expression `obj.property`.
 *
 * Evaluates the object expression, then looks up the named property on it.
 * Used for both attribute access on objects and method lookup.
 */
class PropertyExpr : public Expr {
public:
    /// @brief Construct a PropertyExpr.
    /// @param object    The expression that evaluates to an object.
    /// @param property  The property name (string, not an expression).
    /// @param dot       The '.' token for source position.
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

    std::unique_ptr<Expr> object;  ///< The object expression.
    std::string property;          ///< The property name.
    Token dot;                     ///< The '.' token for diagnostics.
};

/**
 * @brief A property assignment expression `obj.prop = value`.
 *
 * Evaluates the object, then stores the value under the named property.
 * The expression evaluates to the assigned value (assignment is an expression in Vora).
 */
class PropertyAssignmentExpr : public Expr {
public:
    /// @brief Construct a PropertyAssignmentExpr.
    /// @param object    The expression that evaluates to an object.
    /// @param property  The property name to assign to.
    /// @param value     The right-hand side value expression.
    /// @param dot       The '.' token for source position.
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

    std::unique_ptr<Expr> object;   ///< The object expression.
    std::string property;           ///< The property name.
    std::unique_ptr<Expr> value;    ///< The right-hand side value.
    Token dot;                      ///< The '.' token for diagnostics.
};

/**
 * @brief An index assignment expression `obj[index] = value`.
 *
 * Evaluates the object and the index, then stores the value at that key.
 * Works on arrays and dicts. The expression evaluates to the assigned value.
 */
class IndexAssignmentExpr : public Expr {
public:
    /// @brief Construct an IndexAssignmentExpr.
    /// @param object   The expression that evaluates to an indexable value.
    /// @param index    The index/key expression.
    /// @param value    The right-hand side value expression.
    /// @param bracket  The '[' or ']' token for source position.
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

    std::unique_ptr<Expr> object;   ///< The indexable object expression.
    std::unique_ptr<Expr> index;    ///< The index/key expression.
    std::unique_ptr<Expr> value;    ///< The right-hand side value.
    Token bracket;                  ///< Source position for diagnostics.
};

/**
 * @brief The 'this' keyword expression.
 *
 * References the current instance within an object method. At compile time,
 * the compiler resolves 'this' to the instance register.
 */
class ThisExpr : public Expr {
public:
    /// @brief Construct a ThisExpr.
    /// @param keyword  The 'this' token for source position.
    explicit ThisExpr(
        Token keyword
    )
        : keyword(std::move(keyword)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Token keyword;  ///< The 'this' token for diagnostics.
};

/**
 * @brief The 'super' keyword expression.
 *
 * References the parent class within an object method, used for calling
 * superclass methods (e.g., super.init()).
 */
class SuperExpr : public Expr {
public:
    /// @brief Construct a SuperExpr.
    /// @param keyword  The 'super' token for source position.
    explicit SuperExpr(
        Token keyword
    )
        : keyword(std::move(keyword)) {
    }

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    Token keyword;  ///< The 'super' token for diagnostics.
};

/**
 * @brief An increment/decrement expression (++x, x++, --x, x--).
 *
 * Supports both prefix and postfix forms. The target must be a valid
 * l-value (VariableExpr, PropertyExpr, or IndexExpr).
 */
class IncDecExpr : public Expr {
public:
    /// @brief Construct an IncDecExpr.
    /// @param op       The operator token (PLUS_PLUS or MINUS_MINUS).
    /// @param target   The l-value target expression.
    /// @param isPrefix True for ++x (prefix), false for x++ (postfix).
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

    Token op;                    ///< PLUS_PLUS or MINUS_MINUS

    std::unique_ptr<Expr> target;  ///< VariableExpr, PropertyExpr, or IndexExpr

    bool isPrefix;               ///< true = ++x, false = x++
};

/**
 * @brief A ternary conditional expression `condition ? thenBranch : elseBranch`.
 *
 * Evaluates the condition; if truthy, evaluates and returns thenBranch;
 * otherwise evaluates and returns elseBranch. Short-circuits: only one
 * branch is ever evaluated.
 */
class TernaryExpr : public Expr {
public:
    /// @brief Construct a TernaryExpr.
    /// @param condition   The condition expression.
    /// @param thenBranch  The expression evaluated when condition is truthy.
    /// @param elseBranch  The expression evaluated when condition is falsy.
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

    std::unique_ptr<Expr> condition;   ///< The condition expression.

    std::unique_ptr<Expr> thenBranch;  ///< Evaluated when condition is truthy.

    std::unique_ptr<Expr> elseBranch;  ///< Evaluated when condition is falsy.
};

/**
 * @brief Match expression: `match <scrutinee> { <pattern> => <body>, ... }`.
 *
 * Produces a value (expression context). Can also appear as an expression
 * statement where the value is discarded.
 * Patterns are simple tagged unions (not polymorphic) — see MatchPattern.
 */
// Match expression:  match <scrutinee> { <pattern> => <body>, ... }
// Produces a value (expression context). Can also appear as an expression
// statement where the value is discarded.
// Patterns are simple tagged unions (not polymorphic) — see MatchPattern.

struct BlockStmt;

/// @brief Kinds of patterns supported in match expressions.
enum class PatternKind : uint8_t { Literal, Wildcard, Range };

/**
 * @brief A single pattern in a match case.
 *
 * Uses a tagged-union design with `kind` discriminating between literal
 * value match, wildcard (match anything), and range match (lo..hi or lo..=hi).
 */
struct MatchPattern {
    PatternKind kind = PatternKind::Wildcard;  ///< Discriminated union tag.
    Value literal;         ///< Value for Kind::Literal pattern matching.
    Value rangeLow;        ///< Lower bound for Kind::Range patterns.
    Value rangeHigh;       ///< Upper bound for Kind::Range patterns.
    bool rangeInclusive = true;  ///< true = ..= (inclusive), false = .. (exclusive).
    Token start;           ///< First token of the pattern (for error reporting).
};

/**
 * @brief A single case arm in a match expression.
 *
 * Each case has one or more patterns and a body (either an expression or a
 * block). The first matching case wins.
 */
struct MatchCase {
    std::vector<MatchPattern> patterns;   ///< Patterns to match against (at least 1).

    std::unique_ptr<Expr> body;           ///< Expression body (=> expr).

    std::shared_ptr<BlockStmt> blockBody; ///< Block body (=> { ... }).

    Token arrow;                          ///< The '=>' token.
};

/**
 * @brief A match expression `match <scrutinee> { <patterns> => <body>, ... }`.
 *
 * Pattern-matches the scrutinee value against a series of cases. The first
 * matching case's body is evaluated and its value becomes the result of the
 * entire match expression. Cases are evaluated in source order.
 */
class MatchExpr : public Expr {
public:
    /// @brief Construct a MatchExpr.
    /// @param scrutinee    The value to match against.
    /// @param cases        The match case arms.
    /// @param matchKeyword The 'match' token for source position.
    /// @param rightBrace   The '}' token for source position.
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

    std::unique_ptr<Expr> scrutinee;  ///< The value to match.

    std::vector<MatchCase> cases;     ///< Match case arms (evaluated in order).

    Token matchKeyword;               ///< The 'match' token for diagnostics.

    Token rightBrace;                 ///< The '}' token for diagnostics.
};

/**
 * @brief Anonymous function expression (lambda): `func(x, y) { body }`.
 *
 * In expression contexts, `func` without a name creates a lambda that
 * captures the current lexical environment as a closure.
 * Body uses shared_ptr<BlockStmt> — shared_ptr works with incomplete types,
 * avoiding the circular dependency between expr.h and stmt.h.
 */
// Anonymous function expression:  func(x, y) { body }
// In expression contexts, `func` without a name creates a lambda.
// Body uses shared_ptr<BlockStmt> — shared_ptr works with incomplete types,
// avoiding the circular dependency between expr.h and stmt.h.
class FuncExpr : public Expr {
public:
    /// @brief Construct a FuncExpr.
    /// @param params  The parameter declarations.
    /// @param body    The function body block (shared ownership for closure capture).
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

    std::vector<ParamDecl> params;       ///< The parameter declarations.

    std::shared_ptr<BlockStmt> body;     ///< The function body block.
};

/**
 * @brief Generator suspension expression `yield <expr>`.
 *
 * Suspends the current generator function and produces a value to the
 * caller. When the generator is resumed, execution continues after the
 * yield point. A bare `yield` (value is nullptr) yields null.
 */
// yield <expr>  — generator suspension expression
class YieldExpr : public Expr {
public:
    /// @brief Construct a YieldExpr.
    /// @param value    The value to yield (nullptr for bare yield, yields null).
    /// @param keyword  The 'yield' token for source position.
    YieldExpr(std::unique_ptr<Expr> value, Token keyword)
        : value(std::move(value)), keyword(std::move(keyword)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> value;  ///< nullptr = yield with no value (yields null).

    Token keyword;                 ///< The 'yield' token for diagnostics.
};

/**
 * @brief Bare destructuring assignment (without let/const).
 *
 * `[a, b] = arr` or `{x, y} = obj` — unpacks values from an array or object
 * into multiple variables. Pushes the assigned value onto the stack
 * (assignment is an expression in Vora).
 */
// DestructureAssignmentExpr — bare destructuring assignment (without let/const).
// [a, b] = arr   or   {x, y} = obj
// Pushes the assigned value onto the stack (assignment is an expression in Vora).
class DestructureAssignmentExpr : public Expr {
public:
    /// @brief Construct a DestructureAssignmentExpr.
    /// @param binding  The destructuring pattern.
    /// @param value    The source value expression.
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

    std::unique_ptr<BindingPattern> binding;  ///< The destructuring pattern.

    std::unique_ptr<Expr> value;              ///< The source value expression.
};

/**
 * @brief Call-site spread expression `...expr`.
 *
 * Wraps an expression that should be expanded into individual arguments at a
 * call site. Only valid inside CallExpr arguments; appearing elsewhere is a
 * semantic error. f(...arr) compiles arr, then OP_SPREAD to push each element
 * individually.
 */
// SpreadExpr — call-site spread ...expr.
// Wraps an expression that should be expanded into individual arguments at a call site.
// Only valid inside CallExpr arguments; appearing elsewhere is a semantic error.
// f(...arr) → compile arr, then OP_SPREAD to push each element individually.
class SpreadExpr : public Expr {
public:
    /// @brief Construct a SpreadExpr.
    /// @param expr       The expression to spread (must be iterable).
    /// @param dotDotDot  The '...' token for source position tracking.
    SpreadExpr(std::unique_ptr<Expr> expr, Token dotDotDot)
        : expr(std::move(expr)), dotDotDot(std::move(dotDotDot)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> expr;  ///< The iterable expression to spread.

    Token dotDotDot;             ///< The '...' token for source position tracking.
};

/**
 * @brief List comprehension: `[expr for var in iterable if cond]`.
 *
 * Evaluates to a new Array. Desugared by the compiler into a for-in loop
 * that appends to a result array (via OP_ADD array concatenation).
 * The condition is nullptr when no `if` guard is present.
 */
// ListCompExpr — list comprehension: [expr for var in iterable if cond]
// Evaluates to a new Array. Desugared by the compiler into a for-in loop
// that appends to a result array (via OP_ADD array concatenation).
// condition is nullptr when no `if` guard is present.
class ListCompExpr : public Expr {
public:
    /// @brief Construct a ListCompExpr.
    /// @param resultExpr   The result expression for each element.
    /// @param variable     The loop variable name.
    /// @param iterable     The iterable expression to loop over.
    /// @param condition    Optional `if` guard (nullptr when absent).
    /// @param leftBracket  The '[' token for source position.
    ListCompExpr(
        std::unique_ptr<Expr> resultExpr,
        std::string variable,
        std::unique_ptr<Expr> iterable,
        std::unique_ptr<Expr> condition,
        Token leftBracket
    )
        : resultExpr(std::move(resultExpr)),
          variable(std::move(variable)),
          iterable(std::move(iterable)),
          condition(std::move(condition)),
          leftBracket(std::move(leftBracket)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> resultExpr;  ///< Expression for each resulting element.

    std::string variable;              ///< Loop variable name.

    std::unique_ptr<Expr> iterable;    ///< The iterable to loop over.

    std::unique_ptr<Expr> condition;   ///< nullptr if no `if` guard.

    Token leftBracket;                 ///< Source position token.
};

/**
 * @brief Dict comprehension: `{key: val for var in iterable if cond}`.
 *
 * Evaluates to a new Dict. Desugared by the compiler into a for-in loop
 * that merges one-pair dicts (via OP_DICT 1 + OP_ADD merge).
 * The condition is nullptr when no `if` guard is present.
 */
// DictCompExpr — dict comprehension: {key: val for var in iterable if cond}
// Evaluates to a new Dict. Desugared by the compiler into a for-in loop
// that merges one-pair dicts (via OP_DICT 1 + OP_ADD merge).
// condition is nullptr when no `if` guard is present.
class DictCompExpr : public Expr {
public:
    /// @brief Construct a DictCompExpr.
    /// @param keyExpr     The key expression for each entry.
    /// @param valueExpr   The value expression for each entry.
    /// @param variable    The loop variable name.
    /// @param iterable    The iterable expression to loop over.
    /// @param condition   Optional `if` guard (nullptr when absent).
    /// @param leftBrace   The '{' token for source position.
    DictCompExpr(
        std::unique_ptr<Expr> keyExpr,
        std::unique_ptr<Expr> valueExpr,
        std::string variable,
        std::unique_ptr<Expr> iterable,
        std::unique_ptr<Expr> condition,
        Token leftBrace
    )
        : keyExpr(std::move(keyExpr)),
          valueExpr(std::move(valueExpr)),
          variable(std::move(variable)),
          iterable(std::move(iterable)),
          condition(std::move(condition)),
          leftBrace(std::move(leftBrace)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::unique_ptr<Expr> keyExpr;     ///< Key expression for each entry.

    std::unique_ptr<Expr> valueExpr;   ///< Value expression for each entry.

    std::string variable;              ///< Loop variable name.

    std::unique_ptr<Expr> iterable;    ///< The iterable to loop over.

    std::unique_ptr<Expr> condition;   ///< nullptr if no `if` guard.

    Token leftBrace;                   ///< Source position token.
};

/**
 * @brief Placeholder for a parse error in expression context.
 *
 * Used by error-tolerant parsing to retain partial AST structure.
 * Has no meaningful value; visitors that execute code should treat it as null.
 */
// ErrorExpr — placeholder for a parse error in expression context.
// Used by error-tolerant parsing to retain partial AST structure.
// Has no meaningful value; visitors that execute code should treat it as null.
class ErrorExpr : public Expr {
public:
    /// @brief Construct an ErrorExpr.
    /// @param message     The error message.
    /// @param errorToken  The token where the error occurred.
    ErrorExpr(std::string message, Token errorToken)
        : message(std::move(message)), errorToken(std::move(errorToken)) {}

    Value       accept(ExprVisitor<Value>& visitor)       const override;
    void        accept(ExprVisitor<void>& visitor)         const override;
    std::string accept(ExprVisitor<std::string>& visitor) const override;
    std::unique_ptr<Expr> clone() const override;

    std::string message;   ///< The error message.

    Token errorToken;      ///< Source position of the error.
};

/**
 * @brief Optional property/call/index access via `?.` operator.
 *
 * Forms: `a?.b`, `a?.()`, `a?.[i]`. If the object is null, the entire
 * expression short-circuits to null. Otherwise, the operation proceeds
 * normally. Uses a tagged-union design with Kind discriminating between
 * property access, method call, and index access.
 */
// OptionalChainExpr — optional property/call/index access via ?.
// a?.b, a?.(), a?.[i]
// If the object is null, the entire expression short-circuits to null.
// Otherwise, the operation proceeds normally.
class OptionalChainExpr : public Expr {
public:
    /// @brief The kind of optional chain operation.
    enum class Kind : uint8_t { PROPERTY, CALL, INDEX };

    /// @brief Construct an OptionalChainExpr.
    /// @param object       The expression that may be null.
    /// @param kind         The kind of access (PROPERTY, CALL, or INDEX).
    /// @param questionDot  The '?.' token for source position.
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

    std::unique_ptr<Expr> object;   ///< The expression that may be null.

    Kind kind;                       ///< The access kind (PROPERTY, CALL, or INDEX).

    Token questionDot;               ///< the ?. token

    /// @name Kind::PROPERTY fields
    /// @{
    std::string property;            ///< Property name for Kind::PROPERTY.
    /// @}

    /// @name Kind::CALL fields
    /// @{
    std::vector<std::unique_ptr<Expr>> arguments;  ///< Call arguments for Kind::CALL.

    Token closeParen;                ///< Closing ) for Kind::CALL.

    /// Parallel to arguments for Kind::CALL; empty = positional, non-empty = named.
    std::vector<std::string> argumentNames;  ///< Argument names for Kind::CALL.
    /// @}

    /// @name Kind::INDEX fields
    /// @{
    std::unique_ptr<Expr> index;     ///< Index expression for Kind::INDEX.

    Token closeBracket;              ///< Closing ] for Kind::INDEX.
    /// @}
};

}
