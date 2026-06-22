#pragma once

#include "expr.h"

namespace vora {

// ExprVisitor<R>: processes expression AST nodes via double dispatch.
//
// Template parameter R is the return type of each visit* method.
//   - ExprVisitor<void>       → Compiler (emits bytecode)
//   - ExprVisitor<std::string> → ASTPrinter (pretty-prints as S-expression)
//
// To add a new pass with a new return type:
//   1. Add a new accept() overload to Expr (expr.h) + all subclasses (expr.cpp).
//   2. Implement ExprVisitor<NewType> in your pass class.
// The visitor interface itself stays generic — no duplication needed.
template <typename R>
class ExprVisitor {
public:
    using ReturnType = R;

    virtual ~ExprVisitor() = default;

    virtual R visitLiteralExpr(const LiteralExpr& expr) = 0;
    virtual R visitBinaryExpr(const BinaryExpr& expr) = 0;
    virtual R visitGroupingExpr(const GroupingExpr& expr) = 0;
    virtual R visitUnaryExpr(const UnaryExpr& expr) = 0;
    virtual R visitVariableExpr(const VariableExpr& expr) = 0;
    virtual R visitAssignmentExpr(const AssignmentExpr& expr) = 0;
    virtual R visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) = 0;
    virtual R visitCallExpr(const CallExpr& expr) = 0;
    virtual R visitArrayExpr(const ArrayExpr& expr) = 0;
    virtual R visitDictExpr(const DictExpr& expr) = 0;
    virtual R visitIndexExpr(const IndexExpr& expr) = 0;
    virtual R visitPropertyExpr(const PropertyExpr& expr) = 0;
    virtual R visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) = 0;
    virtual R visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) = 0;
    virtual R visitThisExpr(const ThisExpr& expr) = 0;
    virtual R visitSuperExpr(const SuperExpr& expr) = 0;
    virtual R visitIncDecExpr(const IncDecExpr& expr) = 0;
    virtual R visitTernaryExpr(const TernaryExpr& expr) = 0;
    virtual R visitMatchExpr(const MatchExpr& expr) = 0;
    virtual R visitFuncExpr(const FuncExpr& expr) = 0;
    virtual R visitYieldExpr(const YieldExpr& expr) = 0;
    virtual R visitDestructureAssignmentExpr(const DestructureAssignmentExpr& expr) = 0;
    virtual R visitOptionalChainExpr(const OptionalChainExpr& expr) = 0;
    virtual R visitErrorExpr(const ErrorExpr& expr) = 0;
};

}
