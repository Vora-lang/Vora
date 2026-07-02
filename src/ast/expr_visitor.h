#pragma once
/**
 * @file expr_visitor.h
 * @brief Templated visitor interface for expression AST nodes.
 *
 * Defines ExprVisitor<R>, the double-dispatch visitor interface for the Vora
 * expression AST hierarchy. The template parameter R allows compile-time
 * selection of the pass's return type — a single generic interface serves
 * every pass (compiler, pretty-printer, linter, etc.) without per-pass
 * interface duplication.
 *
 * Usage:
 *   - ExprVisitor<void>        — side-effectful passes (e.g. Compiler)
 *   - ExprVisitor<std::string> — passes that produce a string (e.g. ASTPrinter)
 *
 * Adding a new expression node requires:
 *   1. A pure virtual visit*() method here.
 *   2. A new accept() overload in Expr + all subclasses.
 *   3. An implementation in every concrete visitor.
 *
 * @tparam R Return type of each visit* method.
 *
 * @see StmtVisitor, ASTPrinter, Compiler
 */

#include "expr.h"

namespace vora {

/**
 * @brief Visitor interface for expression AST nodes (double dispatch).
 *
 * Each pure virtual method corresponds to one concrete expression subclass.
 * Callers invoke `expr.accept(visitor)`, which dispatches via the vtable to
 * the correct visit*() method, forwarding `*this` with the static type of the
 * concrete node. This eliminates manual `dynamic_cast` chains and type-switch
 * branches.
 *
 * @tparam R The return type shared by every visit* method.
 *           Typically `void` (side-effectful passes) or `std::string`
 *           (analysis passes that produce a value).
 */
template <typename R>
class ExprVisitor {
public:
    /** @brief Convenience typedef for the return type. */
    using ReturnType = R;

    virtual ~ExprVisitor() = default;

    /** @name Literal & Operator Expressions */
    ///@{
    /** @brief Visit a literal value (number, string, bool, null). */
    virtual R visitLiteralExpr(const LiteralExpr& expr) = 0;
    /** @brief Visit a binary operator expression (+, -, *, /, &&, ||, etc.). */
    virtual R visitBinaryExpr(const BinaryExpr& expr) = 0;
    /** @brief Visit a parenthesized grouping expression. */
    virtual R visitGroupingExpr(const GroupingExpr& expr) = 0;
    /** @brief Visit a unary operator expression (!, -, #, typeof). */
    virtual R visitUnaryExpr(const UnaryExpr& expr) = 0;
    /** @brief Visit a ternary conditional expression (cond ? then : else). */
    virtual R visitTernaryExpr(const TernaryExpr& expr) = 0;
    ///@}

    /** @name Variable & Assignment Expressions */
    ///@{
    /** @brief Visit a variable reference expression. */
    virtual R visitVariableExpr(const VariableExpr& expr) = 0;
    /** @brief Visit a simple assignment expression (`x = value`). */
    virtual R visitAssignmentExpr(const AssignmentExpr& expr) = 0;
    /** @brief Visit a compound assignment expression (`x += value`, `x -= value`, etc.). */
    virtual R visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) = 0;
    /** @brief Visit a destructuring assignment (`[a, b] = expr` or `{x, y} = expr`). */
    virtual R visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) = 0;
    ///@}

    /** @name Call & Access Expressions */
    ///@{
    /** @brief Visit a function call expression. */
    virtual R visitCallExpr(const CallExpr& expr) = 0;
    /** @brief Visit an array literal expression. */
    virtual R visitArrayExpr(const ArrayExpr& expr) = 0;
    /** @brief Visit a dictionary literal expression. */
    virtual R visitDictExpr(const DictExpr& expr) = 0;
    /** @brief Visit an index access expression (`expr[key]`). */
    virtual R visitIndexExpr(const IndexExpr& expr) = 0;
    /** @brief Visit a property access expression (`expr.name`). */
    virtual R visitPropertyExpr(const PropertyExpr& expr) = 0;
    /** @brief Visit a property assignment expression (`expr.name = value`). */
    virtual R visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) = 0;
    /** @brief Visit an index assignment expression (`expr[key] = value`). */
    virtual R visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) = 0;
    /** @brief Visit an optional chaining expression (`expr?.prop` or `expr?.[idx]`). */
    virtual R visitOptionalChainExpr(const OptionalChainExpr& expr) = 0;
    ///@}

    /** @name this / super */
    ///@{
    /** @brief Visit a `this` expression (reference to the current instance). */
    virtual R visitThisExpr(const ThisExpr& expr) = 0;
    /** @brief Visit a `super` expression (reference to the parent class). */
    virtual R visitSuperExpr(const SuperExpr& expr) = 0;
    ///@}

    /** @name Increment / Decrement */
    ///@{
    /** @brief Visit a prefix or postfix increment/decrement expression (`i++`, `--x`). */
    virtual R visitIncDecExpr(const IncDecExpr& expr) = 0;
    ///@}

    /** @name Pattern Matching */
    ///@{
    /** @brief Visit a match expression (pattern-matching switch). */
    virtual R visitMatchExpr(const MatchExpr& expr) = 0;
    ///@}

    /** @name Function & Generator Expressions */
    ///@{
    /** @brief Visit an anonymous function expression (lambda). */
    virtual R visitFuncExpr(const FuncExpr& expr) = 0;
    /** @brief Visit a yield expression inside a generator. */
    virtual R visitYieldExpr(const YieldExpr& expr) = 0;
    ///@}

    /** @name Spread */
    ///@{
    /** @brief Visit a spread expression (`...expr` in arrays, dicts, or call args). */
    virtual R visitSpreadExpr(const SpreadExpr& expr) = 0;
    ///@}

    /** @name Comprehensions */
    ///@{
    /** @brief Visit a list comprehension expression (`[expr for x in iterable]`). */
    virtual R visitListCompExpr(const ListCompExpr& expr) = 0;
    /** @brief Visit a dictionary comprehension expression (`{key: val for x in iterable}`). */
    virtual R visitDictCompExpr(const DictCompExpr& expr) = 0;
    ///@}

    /** @name Error Recovery */
    ///@{
    /**
     * @brief Visit an error expression (placeholder from error-tolerant parsing).
     *
     * Compilers should emit `OP_NULL` or a no-op for this node so that partial
     * ASTs remain compilable.
     */
    virtual R visitErrorExpr(const ErrorExpr& expr) = 0;
    ///@}
};

} // namespace vora
