#pragma once

#include <string>
#include <vector>

#include "expr.h"
#include "expr_visitor.h"
#include "stmt.h"
#include "stmt_visitor.h"
#include "program.h"

namespace vora {

class ASTPrinter : public ExprVisitor, public StmtVisitor {
public:
    std::string print(const Expr* expr);
    std::string print(const Stmt* stmt);
    std::string print(const Program* program);

private:
    // Accumulator for the string representation being built.
    // Each visit* method stores its result here; print() returns it
    // immediately after calling accept().
    std::string result_;

    std::string parenthesize(
        const std::string& name,
        const std::vector<const Expr*>& exprs
    );

    // --- ExprVisitor overrides ---
    Value visitLiteralExpr(const LiteralExpr& expr) override;
    Value visitBinaryExpr(const BinaryExpr& expr) override;
    Value visitGroupingExpr(const GroupingExpr& expr) override;
    Value visitUnaryExpr(const UnaryExpr& expr) override;
    Value visitVariableExpr(const VariableExpr& expr) override;
    Value visitAssignmentExpr(const AssignmentExpr& expr) override;
    Value visitCallExpr(const CallExpr& expr) override;
    Value visitArrayExpr(const ArrayExpr& expr) override;
    Value visitIndexExpr(const IndexExpr& expr) override;
    Value visitPropertyExpr(const PropertyExpr& expr) override;
    Value visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) override;
    Value visitThisExpr(const ThisExpr& expr) override;
    Value visitIncDecExpr(const IncDecExpr& expr) override;
    Value visitTernaryExpr(const TernaryExpr& expr) override;

    // --- StmtVisitor overrides ---
    void visitExprStmt(const ExprStmt& stmt) override;
    void visitLetStmt(const LetStmt& stmt) override;
    void visitBlockStmt(const BlockStmt& stmt) override;
    void visitReturnStmt(const ReturnStmt& stmt) override;
    void visitIfStmt(const IfStmt& stmt) override;
    void visitWhileStmt(const WhileStmt& stmt) override;
    void visitForStmt(const ForStmt& stmt) override;
    void visitFuncStmt(const FuncStmt& stmt) override;
    void visitObjStmt(const ObjStmt& stmt) override;
    void visitBreakStmt(const BreakStmt& stmt) override;
    void visitContinueStmt(const ContinueStmt& stmt) override;
    void visitTryStmt(const TryStmt& stmt) override;
    void visitThrowStmt(const ThrowStmt& stmt) override;
};

}
