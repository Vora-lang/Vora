#include "expr.h"
#include "expr_visitor.h"

namespace vora {

Value LiteralExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitLiteralExpr(*this);
}

std::unique_ptr<Expr> LiteralExpr::clone() const {
    return std::make_unique<LiteralExpr>(value);
}

Value BinaryExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitBinaryExpr(*this);
}

std::unique_ptr<Expr> BinaryExpr::clone() const {
    return std::make_unique<BinaryExpr>(
        left->clone(), op, right->clone()
    );
}

Value GroupingExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitGroupingExpr(*this);
}

std::unique_ptr<Expr> GroupingExpr::clone() const {
    return std::make_unique<GroupingExpr>(expression->clone());
}

Value UnaryExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitUnaryExpr(*this);
}

std::unique_ptr<Expr> UnaryExpr::clone() const {
    return std::make_unique<UnaryExpr>(op, right->clone());
}

Value VariableExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitVariableExpr(*this);
}

std::unique_ptr<Expr> VariableExpr::clone() const {
    return std::make_unique<VariableExpr>(name, nameToken);
}

Value AssignmentExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitAssignmentExpr(*this);
}

std::unique_ptr<Expr> AssignmentExpr::clone() const {
    return std::make_unique<AssignmentExpr>(
        name, value->clone(), nameToken
    );
}

Value CallExpr::accept(ExprVisitor& visitor) const {
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

Value ArrayExpr::accept(ExprVisitor& visitor) const {
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

Value IndexExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitIndexExpr(*this);
}

std::unique_ptr<Expr> IndexExpr::clone() const {
    return std::make_unique<IndexExpr>(
        array->clone(), index->clone(), bracket
    );
}

Value PropertyExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitPropertyExpr(*this);
}

std::unique_ptr<Expr> PropertyExpr::clone() const {
    return std::make_unique<PropertyExpr>(
        object->clone(), property, dot
    );
}

Value PropertyAssignmentExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitPropertyAssignmentExpr(*this);
}

std::unique_ptr<Expr> PropertyAssignmentExpr::clone() const {
    return std::make_unique<PropertyAssignmentExpr>(
        object->clone(), property, value->clone(), dot
    );
}

Value ThisExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitThisExpr(*this);
}

std::unique_ptr<Expr> ThisExpr::clone() const {
    return std::make_unique<ThisExpr>(keyword);
}

Value IncDecExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitIncDecExpr(*this);
}

std::unique_ptr<Expr> IncDecExpr::clone() const {
    return std::make_unique<IncDecExpr>(
        op, target->clone(), isPrefix
    );
}

Value TernaryExpr::accept(ExprVisitor& visitor) const {
    return visitor.visitTernaryExpr(*this);
}

std::unique_ptr<Expr> TernaryExpr::clone() const {
    return std::make_unique<TernaryExpr>(
        condition->clone(), thenBranch->clone(), elseBranch->clone()
    );
}

}
