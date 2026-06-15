#pragma once

#include <string>

#include "../ast/expr.h"
#include "../ast/expr_visitor.h"
#include "../ast/stmt.h"
#include "../ast/stmt_visitor.h"
#include "../ast/program.h"

namespace vora {

// SourceFormatter — AST-based source code formatter (vora fmt).
//
// Implements ExprVisitor<std::string>, StmtVisitor<std::string>, and
// ProgramVisitor<std::string> to produce consistently formatted Vora source
// code from any valid AST.
//
// Formatting rules:
//   - 4-space indentation, no tabs
//   - Spaces around binary operators and after keywords (if, while, for, etc.)
//   - No space before ( in calls; space before { in blocks
//   - Braced blocks for all control-flow bodies (always-brace style)
//   - else / else if on the same line as the preceding closing brace
//   - Precedence-aware parenthesization (removes redundant parens, adds
//     necessary ones for correctness)
class SourceFormatter : public ExprVisitor<std::string>,
                        public StmtVisitor<std::string>,
                        public ProgramVisitor<std::string> {
public:
    SourceFormatter() : indent_(0) {}

    // Entry point — format an entire program.
    std::string format(const Program* program);

private:
    int indent_;  // current indentation depth (0-based)

    // --- indentation helpers ---
    std::string indentStr() const;
    std::string nl() const;       // newline + current indent
    void incIndent() { indent_++; }
    void decIndent() { if (indent_ > 0) indent_--; }

    // --- expression formatting with precedence ---

    // Format an expression, wrapping in parentheses if its effective
    // precedence is lower than `minPrec`.
    std::string formatExpr(const Expr& expr, int minPrec);

    // Get the effective precedence of an expression's outermost operator.
    static int exprPrecedence(const Expr& expr);

    // Get the precedence of a token type (same table as Parser::getPrecedence).
    static int tokenPrecedence(TokenType type);

    // Whether a binary token type is right-associative.
    static bool isRightAssoc(TokenType type);

    // --- statement body helpers ---

    // Format a statement as a braced block body. If `stmt` is already a
    // BlockStmt, indents and formats it. Otherwise wraps it in braces.
    std::string formatBlockBody(const Stmt& stmt);

    // Format a list of statements as a block body (for use when we already
    // have a statement vector, e.g. BlockStmt, Program).
    std::string formatStatements(const std::vector<std::unique_ptr<Stmt>>& stmts);

    // Format a function-like parameter list: "(p1, p2, ...)".
    std::string formatParams(const std::vector<ParamDecl>& params);

    // =====================================================================
    // ExprVisitor<std::string> overrides
    // =====================================================================

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
    std::string visitFuncExpr(const FuncExpr& expr) override;

    // =====================================================================
    // StmtVisitor<std::string> overrides
    // =====================================================================

    std::string visitExprStmt(const ExprStmt& stmt) override;
    std::string visitLetStmt(const LetStmt& stmt) override;
    std::string visitBlockStmt(const BlockStmt& stmt) override;
    std::string visitReturnStmt(const ReturnStmt& stmt) override;
    std::string visitIfStmt(const IfStmt& stmt) override;
    std::string visitWhileStmt(const WhileStmt& stmt) override;
    std::string visitForStmt(const ForStmt& stmt) override;
    std::string visitCForStmt(const CForStmt& stmt) override;
    std::string visitFuncStmt(const FuncStmt& stmt) override;
    std::string visitObjStmt(const ObjStmt& stmt) override;
    std::string visitBreakStmt(const BreakStmt& stmt) override;
    std::string visitContinueStmt(const ContinueStmt& stmt) override;
    std::string visitTryStmt(const TryStmt& stmt) override;
    std::string visitThrowStmt(const ThrowStmt& stmt) override;

    // =====================================================================
    // ProgramVisitor<std::string> override
    // =====================================================================

    std::string visitProgram(const Program& program) override;
};

}  // namespace vora
