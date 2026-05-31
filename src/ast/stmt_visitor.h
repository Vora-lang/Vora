#pragma once

#include "stmt.h"

namespace vora {

// StmtVisitor: processes statement AST nodes via double dispatch.
// Each visit method returns void — statements execute for side effects.
//
// Future passes that need different semantics should define their own visitor
// interface and add a corresponding accept() overload to the Stmt base class.
class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;

    virtual void visitExprStmt(const ExprStmt& stmt) = 0;
    virtual void visitLetStmt(const LetStmt& stmt) = 0;
    virtual void visitBlockStmt(const BlockStmt& stmt) = 0;
    virtual void visitReturnStmt(const ReturnStmt& stmt) = 0;
    virtual void visitIfStmt(const IfStmt& stmt) = 0;
    virtual void visitWhileStmt(const WhileStmt& stmt) = 0;
    virtual void visitForStmt(const ForStmt& stmt) = 0;
    virtual void visitFuncStmt(const FuncStmt& stmt) = 0;
    virtual void visitObjStmt(const ObjStmt& stmt) = 0;
    virtual void visitBreakStmt(const BreakStmt& stmt) = 0;
    virtual void visitContinueStmt(const ContinueStmt& stmt) = 0;
    virtual void visitTryStmt(const TryStmt& stmt) = 0;
    virtual void visitThrowStmt(const ThrowStmt& stmt) = 0;
};

}
