#pragma once
/**
 * @file stmt_visitor.h
 * @brief Templated visitor interface for statement AST nodes.
 *
 * Defines StmtVisitor<R>, the double-dispatch visitor interface for the Vora
 * statement AST hierarchy. The template parameter R allows compile-time
 * selection of the pass's return type — a single generic interface serves
 * every pass (compiler, pretty-printer, linter, etc.) without per-pass
 * interface duplication.
 *
 * Usage:
 *   - StmtVisitor<void>        — side-effectful passes (e.g. Compiler)
 *   - StmtVisitor<std::string> — passes that produce a string (e.g. ASTPrinter)
 *
 * Adding a new statement node requires:
 *   1. A pure virtual visit*() method here.
 *   2. A new accept() overload in Stmt + all subclasses.
 *   3. An implementation in every concrete visitor.
 *
 * @tparam R Return type of each visit* method.
 *
 * @see ExprVisitor, ASTPrinter, Compiler
 */

#include "stmt.h"

namespace vora {

/**
 * @brief Visitor interface for statement AST nodes (double dispatch).
 *
 * Each pure virtual method corresponds to one concrete statement subclass.
 * Callers invoke `stmt.accept(visitor)`, which dispatches via the vtable to
 * the correct visit*() method, forwarding `*this` with the static type of the
 * concrete node. This eliminates manual `dynamic_cast` chains and type-switch
 * branches.
 *
 * @tparam R The return type shared by every visit* method.
 *           Typically `void` (side-effectful passes) or `std::string`
 *           (analysis passes that produce a value).
 */
template <typename R>
class StmtVisitor {
public:
    /** @brief Convenience typedef for the return type. */
    using ReturnType = R;

    virtual ~StmtVisitor() = default;

    /** @name Core Statement Types */
    ///@{
    /**
     * @brief Visit an expression statement (an expression used as a statement).
     *
     * Covers cases like `print(x);` where a call/assignment/etc. appears at
     * statement level.
     */
    virtual R visitExprStmt(const ExprStmt& stmt) = 0;
    /**
     * @brief Visit a variable declaration statement (`let x = 5;` or `const y = 10;`).
     *
     * Covers both `let` and `const` declarations. The `isConst` flag on the
     * node distinguishes mutable vs. immutable bindings.
     */
    virtual R visitLetStmt(const LetStmt& stmt) = 0;
    /** @brief Visit a block statement (`{ ... }`). */
    virtual R visitBlockStmt(const BlockStmt& stmt) = 0;
    ///@}

    /** @name Control Flow */
    ///@{
    /** @brief Visit a return statement. */
    virtual R visitReturnStmt(const ReturnStmt& stmt) = 0;
    /** @brief Visit an if/else-if/else statement. */
    virtual R visitIfStmt(const IfStmt& stmt) = 0;
    /** @brief Visit a while loop statement. */
    virtual R visitWhileStmt(const WhileStmt& stmt) = 0;
    /** @brief Visit a do-while loop statement. */
    virtual R visitDoWhileStmt(const DoWhileStmt& stmt) = 0;
    /** @brief Visit a for-in loop statement (`for x in iterable`). */
    virtual R visitForStmt(const ForStmt& stmt) = 0;
    /** @brief Visit a C-style for statement (`for (init; cond; incr)`). */
    virtual R visitCForStmt(const CForStmt& stmt) = 0;
    ///@}

    /** @name Functions & Objects */
    ///@{
    /** @brief Visit a named function declaration statement. */
    virtual R visitFuncStmt(const FuncStmt& stmt) = 0;
    /** @brief Visit an object/class declaration statement. */
    virtual R visitObjStmt(const ObjStmt& stmt) = 0;
    ///@}

    /** @name Break & Continue */
    ///@{
    /** @brief Visit a break statement (exit loop). */
    virtual R visitBreakStmt(const BreakStmt& stmt) = 0;
    /** @brief Visit a continue statement (skip to next iteration). */
    virtual R visitContinueStmt(const ContinueStmt& stmt) = 0;
    ///@}

    /** @name Exception Handling */
    ///@{
    /** @brief Visit a try/catch/finally statement. */
    virtual R visitTryStmt(const TryStmt& stmt) = 0;
    /** @brief Visit a throw statement. */
    virtual R visitThrowStmt(const ThrowStmt& stmt) = 0;
    ///@}

    /** @name Modules */
    ///@{
    /** @brief Visit an import statement (`import "module"` or `import { x } from "mod"`). */
    virtual R visitImportStmt(const ImportStmt& stmt) = 0;
    /** @brief Visit an export statement (`export { x, y }`). */
    virtual R visitExportStmt(const ExportStmt& stmt) = 0;
    ///@}

    /** @name Deferred Execution */
    ///@{
    /** @brief Visit a defer statement (`defer cleanup()` — runs at scope exit). */
    virtual R visitDeferStmt(const DeferStmt& stmt) = 0;
    ///@}

    /** @name Error Recovery */
    ///@{
    /**
     * @brief Visit an error statement (placeholder from error-tolerant parsing).
     *
     * Compilers should emit a no-op for this node so that partial ASTs remain
     * compilable.
     */
    virtual R visitErrorStmt(const ErrorStmt& stmt) = 0;
    ///@}
};

} // namespace vora
