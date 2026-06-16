#include "stmt.h"
#include "stmt_visitor.h"

namespace vora {

// =========================================================================
// ExprStmt
// =========================================================================

void ExprStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitExprStmt(*this);
}

std::string ExprStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitExprStmt(*this);
}

// =========================================================================
// LetStmt
// =========================================================================

void LetStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitLetStmt(*this);
}

std::string LetStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitLetStmt(*this);
}

// =========================================================================
// BlockStmt
// =========================================================================

void BlockStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitBlockStmt(*this);
}

std::string BlockStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitBlockStmt(*this);
}

// =========================================================================
// ReturnStmt
// =========================================================================

void ReturnStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitReturnStmt(*this);
}

std::string ReturnStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitReturnStmt(*this);
}

// =========================================================================
// IfStmt
// =========================================================================

void IfStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitIfStmt(*this);
}

std::string IfStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitIfStmt(*this);
}

// =========================================================================
// WhileStmt
// =========================================================================

void WhileStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitWhileStmt(*this);
}

std::string WhileStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitWhileStmt(*this);
}

// =========================================================================
// ForStmt
// =========================================================================

void ForStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitForStmt(*this);
}

std::string ForStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitForStmt(*this);
}

// =========================================================================
// CForStmt
// =========================================================================

void CForStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitCForStmt(*this);
}

std::string CForStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitCForStmt(*this);
}

// =========================================================================
// FuncStmt
// =========================================================================

void FuncStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitFuncStmt(*this);
}

std::string FuncStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitFuncStmt(*this);
}

// =========================================================================
// ObjStmt
// =========================================================================

void ObjStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitObjStmt(*this);
}

std::string ObjStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitObjStmt(*this);
}

// =========================================================================
// BreakStmt
// =========================================================================

void BreakStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitBreakStmt(*this);
}

std::string BreakStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitBreakStmt(*this);
}

// =========================================================================
// ContinueStmt
// =========================================================================

void ContinueStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitContinueStmt(*this);
}

std::string ContinueStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitContinueStmt(*this);
}

// =========================================================================
// ThrowStmt
// =========================================================================

void ThrowStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitThrowStmt(*this);
}

std::string ThrowStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitThrowStmt(*this);
}

// =========================================================================
// TryStmt
// =========================================================================

void TryStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitTryStmt(*this);
}

std::string TryStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitTryStmt(*this);
}

// =========================================================================
// ImportStmt
// =========================================================================

void ImportStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitImportStmt(*this);
}

std::string ImportStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitImportStmt(*this);
}

// =========================================================================
// ExportStmt
// =========================================================================

void ExportStmt::accept(StmtVisitor<void>& visitor) const {
    visitor.visitExportStmt(*this);
}

std::string ExportStmt::accept(StmtVisitor<std::string>& visitor) const {
    return visitor.visitExportStmt(*this);
}

}
