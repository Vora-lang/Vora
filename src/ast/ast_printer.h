#pragma once
/**
 * @file ast_printer.h
 * @brief AST pretty-printer producing S-expression debug output.
 *
 * ASTPrinter implements all three visitor interfaces (ExprVisitor<std::string>,
 * StmtVisitor<std::string>, ProgramVisitor<std::string>) to produce a
 * human-readable S-expression representation of the entire AST. This is used
 * primarily for debugging and testing — the output is NOT a source-code
 * formatter (use SourceFormatter for that).
 *
 * Example output for `1 + 2 * 3`:
 *   (binary + (literal 1) (binary * (literal 2) (literal 3)))
 *
 * @see SourceFormatter (src/formatter/) for source-level formatting.
 */

#include <string>
#include <vector>

#include "expr.h"
#include "expr_visitor.h"
#include "stmt.h"
#include "stmt_visitor.h"
#include "program.h"

namespace vora {

/**
 * @brief Pretty-prints AST nodes as Lisp-style S-expressions.
 *
 * Implements ExprVisitor, StmtVisitor, and ProgramVisitor with return type
 * `std::string`. Each visit* method recursively prints its children and wraps
 * the result in a parenthesized form with the node type as the first element.
 *
 * Thread-safety: Each ASTPrinter instance is NOT thread-safe (depth_ is
 * mutated during printing). Create a separate instance per thread or per
 * print job.
 */
class ASTPrinter : public ExprVisitor<std::string>,
                   public StmtVisitor<std::string>,
                   public ProgramVisitor<std::string> {
public:
    /**
     * @brief Print an expression node to an S-expression string.
     * @param expr Pointer to the expression (must not be null).
     * @return The S-expression string representation.
     */
    std::string print(const Expr* expr);

    /**
     * @brief Print a statement node to an S-expression string.
     * @param stmt Pointer to the statement (must not be null).
     * @return The S-expression string representation.
     */
    std::string print(const Stmt* stmt);

    /**
     * @brief Print an entire program (top-level statement list) to an S-expression string.
     * @param program Pointer to the Program node (must not be null).
     * @return The S-expression string representation.
     */
    std::string print(const Program* program);

private:
    /** @brief Maximum recursion depth to prevent stack overflow on malformed ASTs. */
    static constexpr int MAX_PRINT_DEPTH = 10000;

    /** @brief Current recursion depth; checked against MAX_PRINT_DEPTH at each level. */
    int depth_ = 0;

    /**
     * @brief Format a list of sub-expressions as a parenthesized S-expression.
     *
     * Produces `(name child1 child2 ...)`. Each child expression is printed
     * via `child->accept(*this)`.
     *
     * @param name  The node type label (e.g. "binary", "+", "call").
     * @param exprs Vector of child expressions to print (nullptrs produce "<error>").
     * @return The formatted S-expression string.
     */
    std::string parenthesize(
        const std::string& name,
        const std::vector<const Expr*>& exprs
    );

    // --- ExprVisitor<std::string> overrides ---
    std::string visitLiteralExpr(const LiteralExpr& expr) override;
    std::string visitBinaryExpr(const BinaryExpr& expr) override;
    std::string visitGroupingExpr(const GroupingExpr& expr) override;
    std::string visitUnaryExpr(const UnaryExpr& expr) override;
    std::string visitVariableExpr(const VariableExpr& expr) override;
    std::string visitAssignmentExpr(const AssignmentExpr& expr) override;
    std::string visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) override;
    std::string visitCallExpr(const CallExpr& expr) override;
    std::string visitArrayExpr(const ArrayExpr& expr) override;
    std::string visitDictExpr(const DictExpr& expr) override;
    std::string visitIndexExpr(const IndexExpr& expr) override;
    std::string visitPropertyExpr(const PropertyExpr& expr) override;
    std::string visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    std::string visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) override;
    std::string visitThisExpr(const ThisExpr& expr) override;
    std::string visitSuperExpr(const SuperExpr& expr) override;
    std::string visitIncDecExpr(const IncDecExpr& expr) override;
    std::string visitTernaryExpr(const TernaryExpr& expr) override;
    std::string visitMatchExpr(const MatchExpr& expr) override;
    std::string visitFuncExpr(const FuncExpr& expr) override;
    std::string visitYieldExpr(const YieldExpr& expr) override;
    std::string visitAwaitExpr(const AwaitExpr& expr) override;
    std::string visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) override;
    std::string visitSpreadExpr(const SpreadExpr& expr) override;
    std::string visitListCompExpr(const ListCompExpr& expr) override;
    std::string visitDictCompExpr(const DictCompExpr& expr) override;
    std::string visitOptionalChainExpr(const OptionalChainExpr& expr) override;
    std::string visitErrorExpr(const ErrorExpr& expr) override;

    // --- StmtVisitor<std::string> overrides ---
    std::string visitExprStmt(const ExprStmt& stmt) override;
    std::string visitLetStmt(const LetStmt& stmt) override;
    std::string visitBlockStmt(const BlockStmt& stmt) override;
    std::string visitReturnStmt(const ReturnStmt& stmt) override;
    std::string visitIfStmt(const IfStmt& stmt) override;
    std::string visitWhileStmt(const WhileStmt& stmt) override;
    std::string visitDoWhileStmt(const DoWhileStmt& stmt) override;
    std::string visitForStmt(const ForStmt& stmt) override;
    std::string visitCForStmt(const CForStmt& stmt) override;
    std::string visitFuncStmt(const FuncStmt& stmt) override;
    std::string visitObjStmt(const ObjStmt& stmt) override;
    std::string visitBreakStmt(const BreakStmt& stmt) override;
    std::string visitContinueStmt(const ContinueStmt& stmt) override;
    std::string visitTryStmt(const TryStmt& stmt) override;
    std::string visitThrowStmt(const ThrowStmt& stmt) override;
    std::string visitImportStmt(const ImportStmt& stmt) override;
    std::string visitExportStmt(const ExportStmt& stmt) override;
    std::string visitDeferStmt(const DeferStmt& stmt) override;
    std::string visitErrorStmt(const ErrorStmt& stmt) override;

    // --- ProgramVisitor<std::string> override ---
    std::string visitProgram(const Program& program) override;
};

} // namespace vora
