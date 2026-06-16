#pragma once

#include <string>
#include <vector>

#include "expr.h"
#include "expr_visitor.h"
#include "stmt.h"
#include "stmt_visitor.h"
#include "program.h"

namespace vora {

class ASTPrinter : public ExprVisitor<std::string>,
                   public StmtVisitor<std::string>,
                   public ProgramVisitor<std::string> {
public:
    std::string print(const Expr* expr);
    std::string print(const Stmt* stmt);
    std::string print(const Program* program);

private:
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
    std::string visitFuncExpr(const FuncExpr& expr) override;
    std::string visitYieldExpr(const YieldExpr& expr) override;

    // --- StmtVisitor<std::string> overrides ---
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
    std::string visitImportStmt(const ImportStmt& stmt) override;
    std::string visitExportStmt(const ExportStmt& stmt) override;

    // --- ProgramVisitor<std::string> override ---
    std::string visitProgram(const Program& program) override;
};

}
