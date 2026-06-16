#include "expr.h"
#include "expr_visitor.h"
#include "stmt.h"  // for FuncExpr — needs complete BlockStmt

namespace vora {

// =========================================================================
// LiteralExpr
// =========================================================================

Value LiteralExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitLiteralExpr(*this);
}

void LiteralExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitLiteralExpr(*this);
}

std::string LiteralExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitLiteralExpr(*this);
}

std::unique_ptr<Expr> LiteralExpr::clone() const {
    return std::make_unique<LiteralExpr>(value);
}

// =========================================================================
// BinaryExpr
// =========================================================================

Value BinaryExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitBinaryExpr(*this);
}

void BinaryExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitBinaryExpr(*this);
}

std::string BinaryExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitBinaryExpr(*this);
}

std::unique_ptr<Expr> BinaryExpr::clone() const {
    return std::make_unique<BinaryExpr>(
        left->clone(), op, right->clone()
    );
}

// =========================================================================
// GroupingExpr
// =========================================================================

Value GroupingExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitGroupingExpr(*this);
}

void GroupingExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitGroupingExpr(*this);
}

std::string GroupingExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitGroupingExpr(*this);
}

std::unique_ptr<Expr> GroupingExpr::clone() const {
    return std::make_unique<GroupingExpr>(expression->clone());
}

// =========================================================================
// UnaryExpr
// =========================================================================

Value UnaryExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitUnaryExpr(*this);
}

void UnaryExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitUnaryExpr(*this);
}

std::string UnaryExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitUnaryExpr(*this);
}

std::unique_ptr<Expr> UnaryExpr::clone() const {
    return std::make_unique<UnaryExpr>(op, right->clone());
}

// =========================================================================
// VariableExpr
// =========================================================================

Value VariableExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitVariableExpr(*this);
}

void VariableExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitVariableExpr(*this);
}

std::string VariableExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitVariableExpr(*this);
}

std::unique_ptr<Expr> VariableExpr::clone() const {
    return std::make_unique<VariableExpr>(name, nameToken);
}

// =========================================================================
// AssignmentExpr
// =========================================================================

Value AssignmentExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitAssignmentExpr(*this);
}

void AssignmentExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitAssignmentExpr(*this);
}

std::string AssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitAssignmentExpr(*this);
}

std::unique_ptr<Expr> AssignmentExpr::clone() const {
    return std::make_unique<AssignmentExpr>(
        name, value->clone(), nameToken
    );
}

// =========================================================================
// CompoundAssignmentExpr
// =========================================================================

Value CompoundAssignmentExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitCompoundAssignmentExpr(*this);
}

void CompoundAssignmentExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitCompoundAssignmentExpr(*this);
}

std::string CompoundAssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitCompoundAssignmentExpr(*this);
}

std::unique_ptr<Expr> CompoundAssignmentExpr::clone() const {
    return std::make_unique<CompoundAssignmentExpr>(
        target->clone(), op, value->clone()
    );
}

// =========================================================================
// CallExpr
// =========================================================================

Value CallExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitCallExpr(*this);
}

void CallExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitCallExpr(*this);
}

std::string CallExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitCallExpr(*this);
}

std::unique_ptr<Expr> CallExpr::clone() const {
    std::vector<std::unique_ptr<Expr>> clonedArgs;
    for (const auto& arg : arguments) {
        clonedArgs.push_back(arg->clone());
    }
    return std::make_unique<CallExpr>(
        callee->clone(), std::move(clonedArgs), paren
    );
}

// =========================================================================
// ArrayExpr
// =========================================================================

Value ArrayExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitArrayExpr(*this);
}

void ArrayExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitArrayExpr(*this);
}

std::string ArrayExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitArrayExpr(*this);
}

std::unique_ptr<Expr> ArrayExpr::clone() const {
    std::vector<std::unique_ptr<Expr>> clonedElements;
    for (const auto& elem : elements) {
        clonedElements.push_back(elem->clone());
    }
    return std::make_unique<ArrayExpr>(
        std::move(clonedElements), leftBracket
    );
}

// =========================================================================
// DictExpr
// =========================================================================

Value DictExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitDictExpr(*this);
}

void DictExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitDictExpr(*this);
}

std::string DictExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitDictExpr(*this);
}

std::unique_ptr<Expr> DictExpr::clone() const {
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> clonedPairs;
    for (const auto& [k, v] : pairs) {
        clonedPairs.push_back({k->clone(), v->clone()});
    }
    return std::make_unique<DictExpr>(std::move(clonedPairs), leftBrace);
}

// =========================================================================
// IndexExpr
// =========================================================================

Value IndexExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitIndexExpr(*this);
}

void IndexExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitIndexExpr(*this);
}

std::string IndexExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitIndexExpr(*this);
}

std::unique_ptr<Expr> IndexExpr::clone() const {
    return std::make_unique<IndexExpr>(
        array->clone(), index->clone(), bracket
    );
}

// =========================================================================
// PropertyExpr
// =========================================================================

Value PropertyExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitPropertyExpr(*this);
}

void PropertyExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitPropertyExpr(*this);
}

std::string PropertyExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitPropertyExpr(*this);
}

std::unique_ptr<Expr> PropertyExpr::clone() const {
    return std::make_unique<PropertyExpr>(
        object->clone(), property, dot
    );
}

// =========================================================================
// PropertyAssignmentExpr
// =========================================================================

Value PropertyAssignmentExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitPropertyAssignmentExpr(*this);
}

void PropertyAssignmentExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitPropertyAssignmentExpr(*this);
}

std::string PropertyAssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitPropertyAssignmentExpr(*this);
}

std::unique_ptr<Expr> PropertyAssignmentExpr::clone() const {
    return std::make_unique<PropertyAssignmentExpr>(
        object->clone(), property, value->clone(), dot
    );
}

// =========================================================================
// IndexAssignmentExpr
// =========================================================================

Value IndexAssignmentExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitIndexAssignmentExpr(*this);
}

void IndexAssignmentExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitIndexAssignmentExpr(*this);
}

std::string IndexAssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitIndexAssignmentExpr(*this);
}

std::unique_ptr<Expr> IndexAssignmentExpr::clone() const {
    return std::make_unique<IndexAssignmentExpr>(
        object->clone(), index->clone(), value->clone(), bracket
    );
}

// =========================================================================
// ThisExpr
// =========================================================================

Value ThisExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitThisExpr(*this);
}

void ThisExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitThisExpr(*this);
}

std::string ThisExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitThisExpr(*this);
}

std::unique_ptr<Expr> ThisExpr::clone() const {
    return std::make_unique<ThisExpr>(keyword);
}

// =========================================================================
// SuperExpr
// =========================================================================

Value SuperExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitSuperExpr(*this);
}

void SuperExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitSuperExpr(*this);
}

std::string SuperExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitSuperExpr(*this);
}

std::unique_ptr<Expr> SuperExpr::clone() const {
    return std::make_unique<SuperExpr>(keyword);
}

// =========================================================================
// IncDecExpr
// =========================================================================

Value IncDecExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitIncDecExpr(*this);
}

void IncDecExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitIncDecExpr(*this);
}

std::string IncDecExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitIncDecExpr(*this);
}

std::unique_ptr<Expr> IncDecExpr::clone() const {
    return std::make_unique<IncDecExpr>(
        op, target->clone(), isPrefix
    );
}

// =========================================================================
// TernaryExpr
// =========================================================================

Value TernaryExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitTernaryExpr(*this);
}

void TernaryExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitTernaryExpr(*this);
}

std::string TernaryExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitTernaryExpr(*this);
}

std::unique_ptr<Expr> TernaryExpr::clone() const {
    return std::make_unique<TernaryExpr>(
        condition->clone(), thenBranch->clone(), elseBranch->clone()
    );
}

// =========================================================================
// FuncExpr
// =========================================================================

Value FuncExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitFuncExpr(*this);
}

void FuncExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitFuncExpr(*this);
}

std::string FuncExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitFuncExpr(*this);
}

std::unique_ptr<Expr> FuncExpr::clone() const {
    std::vector<ParamDecl> clonedParams;
    for (const auto& p : params) {
        clonedParams.emplace_back(p.name, p.defaultValue ? p.defaultValue->clone() : nullptr);
    }
    // BlockStmt has no clone; body is shared (shallow copy via shared_ptr)
    return std::make_unique<FuncExpr>(std::move(clonedParams), body);
}

// =========================================================================
// YieldExpr
// =========================================================================

Value YieldExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitYieldExpr(*this);
}

void YieldExpr::accept(ExprVisitor<void>& visitor) const {
    visitor.visitYieldExpr(*this);
}

std::string YieldExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitYieldExpr(*this);
}

std::unique_ptr<Expr> YieldExpr::clone() const {
    return std::make_unique<YieldExpr>(
        value ? value->clone() : nullptr,
        keyword
    );
}

}
