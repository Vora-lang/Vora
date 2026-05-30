#pragma once

#include "expr.h"

namespace vora {

// ExprVisitor: processes expression AST nodes via double dispatch.
// Each visit method returns a Value — the result of evaluating that expression.
//
// Future passes that need different return types (e.g., void for a bytecode
// compiler) should define their own visitor interface and add a corresponding
// accept() overload to the Expr base class.
class ExprVisitor {
public:
    virtual ~ExprVisitor() = default;

    virtual Value visitLiteralExpr(const LiteralExpr& expr) = 0;
    virtual Value visitBinaryExpr(const BinaryExpr& expr) = 0;
    virtual Value visitGroupingExpr(const GroupingExpr& expr) = 0;
    virtual Value visitUnaryExpr(const UnaryExpr& expr) = 0;
    virtual Value visitVariableExpr(const VariableExpr& expr) = 0;
    virtual Value visitAssignmentExpr(const AssignmentExpr& expr) = 0;
    virtual Value visitCallExpr(const CallExpr& expr) = 0;
    virtual Value visitArrayExpr(const ArrayExpr& expr) = 0;
    virtual Value visitIndexExpr(const IndexExpr& expr) = 0;
    virtual Value visitPropertyExpr(const PropertyExpr& expr) = 0;
    virtual Value visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) = 0;
    virtual Value visitThisExpr(const ThisExpr& expr) = 0;
    virtual Value visitIncDecExpr(const IncDecExpr& expr) = 0;
};

}
