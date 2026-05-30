#include "stmt.h"
#include "stmt_visitor.h"

namespace vora {

void ExprStmt::accept(StmtVisitor& visitor) const {
    visitor.visitExprStmt(*this);
}

void LetStmt::accept(StmtVisitor& visitor) const {
    visitor.visitLetStmt(*this);
}

void BlockStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBlockStmt(*this);
}

void ReturnStmt::accept(StmtVisitor& visitor) const {
    visitor.visitReturnStmt(*this);
}

void IfStmt::accept(StmtVisitor& visitor) const {
    visitor.visitIfStmt(*this);
}

void WhileStmt::accept(StmtVisitor& visitor) const {
    visitor.visitWhileStmt(*this);
}

void ForStmt::accept(StmtVisitor& visitor) const {
    visitor.visitForStmt(*this);
}

void FuncStmt::accept(StmtVisitor& visitor) const {
    visitor.visitFuncStmt(*this);
}

void ObjStmt::accept(StmtVisitor& visitor) const {
    visitor.visitObjStmt(*this);
}

void BreakStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBreakStmt(*this);
}

void ContinueStmt::accept(StmtVisitor& visitor) const {
    visitor.visitContinueStmt(*this);
}

}
