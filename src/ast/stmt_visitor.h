#pragma once

#include "stmt.h"

namespace vora {

// StmtVisitor<R>: processes statement AST nodes via double dispatch.
//
// Template parameter R is the return type of each visit* method.
//   - StmtVisitor<void>         → Compiler (emits bytecode)
//   - StmtVisitor<std::string>  → ASTPrinter (pretty-prints as S-expression)
//
// To add a new pass with a new return type:
//   1. Add a new accept() overload to Stmt (stmt.h) + all subclasses (stmt.cpp).
//   2. Implement StmtVisitor<NewType> in your pass class.
// The visitor interface itself stays generic — no duplication needed.
template <typename R>
class StmtVisitor {
public:
    using ReturnType = R;

    virtual ~StmtVisitor() = default;

    virtual R visitExprStmt(const ExprStmt& stmt) = 0;
    virtual R visitLetStmt(const LetStmt& stmt) = 0;
    virtual R visitBlockStmt(const BlockStmt& stmt) = 0;
    virtual R visitReturnStmt(const ReturnStmt& stmt) = 0;
    virtual R visitIfStmt(const IfStmt& stmt) = 0;
    virtual R visitWhileStmt(const WhileStmt& stmt) = 0;
    virtual R visitForStmt(const ForStmt& stmt) = 0;
    virtual R visitCForStmt(const CForStmt& stmt) = 0;
    virtual R visitFuncStmt(const FuncStmt& stmt) = 0;
    virtual R visitObjStmt(const ObjStmt& stmt) = 0;
    virtual R visitBreakStmt(const BreakStmt& stmt) = 0;
    virtual R visitContinueStmt(const ContinueStmt& stmt) = 0;
    virtual R visitTryStmt(const TryStmt& stmt) = 0;
    virtual R visitThrowStmt(const ThrowStmt& stmt) = 0;
    virtual R visitImportStmt(const ImportStmt& stmt) = 0;
    virtual R visitExportStmt(const ExportStmt& stmt) = 0;
    virtual R visitErrorStmt(const ErrorStmt& stmt) = 0;
};

}
