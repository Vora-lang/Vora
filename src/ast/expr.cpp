#include "expr.h"
#include "expr_visitor.h"

namespace vora {

// =========================================================================
// LiteralExpr
// =========================================================================

Value LiteralExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitLiteralExpr(*this);
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

std::string AssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitAssignmentExpr(*this);
}

std::unique_ptr<Expr> AssignmentExpr::clone() const {
    return std::make_unique<AssignmentExpr>(
        name, value->clone(), nameToken
    );
}

// =========================================================================
// CallExpr
// =========================================================================

Value CallExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitCallExpr(*this);
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
// IndexExpr
// =========================================================================

Value IndexExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitIndexExpr(*this);
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

std::string PropertyAssignmentExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitPropertyAssignmentExpr(*this);
}

std::unique_ptr<Expr> PropertyAssignmentExpr::clone() const {
    return std::make_unique<PropertyAssignmentExpr>(
        object->clone(), property, value->clone(), dot
    );
}

// =========================================================================
// ThisExpr
// =========================================================================

Value ThisExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitThisExpr(*this);
}

std::string ThisExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitThisExpr(*this);
}

std::unique_ptr<Expr> ThisExpr::clone() const {
    return std::make_unique<ThisExpr>(keyword);
}

// =========================================================================
// IncDecExpr
// =========================================================================

Value IncDecExpr::accept(ExprVisitor<Value>& visitor) const {
    return visitor.visitIncDecExpr(*this);
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

std::string TernaryExpr::accept(ExprVisitor<std::string>& visitor) const {
    return visitor.visitTernaryExpr(*this);
}

std::unique_ptr<Expr> TernaryExpr::clone() const {
    return std::make_unique<TernaryExpr>(
        condition->clone(), thenBranch->clone(), elseBranch->clone()
    );
}

}
