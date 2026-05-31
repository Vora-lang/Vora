#include "ast_printer.h"
#include "program.h"

#include "../runtime/callable.h"

#include <sstream>

namespace vora {

// =========================================================================
// Entry points — delegate via double dispatch, return accumulated result
// =========================================================================

std::string ASTPrinter::print(const Expr* expr) {
    expr->accept(*this);
    return result_;
}

std::string ASTPrinter::print(const Stmt* stmt) {
    stmt->accept(*this);
    return result_;
}

// =========================================================================
// ExprVisitor — visit methods
// =========================================================================

Value ASTPrinter::visitLiteralExpr(const LiteralExpr& expr) {

    result_ = std::visit([](auto&& arg) -> std::string {

        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            std::string out = "[";
            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += std::visit([](auto&& inner) -> std::string {
                    using U = std::decay_t<decltype(inner)>;
                    if constexpr (std::is_same_v<U, std::nullptr_t>) return "null";
                    else if constexpr (std::is_same_v<U, bool>) return inner ? "true" : "false";
                    else if constexpr (std::is_same_v<U, std::string>) return inner;
                    else if constexpr (std::is_same_v<U, std::shared_ptr<Callable>>) return "<fn>";
                    else if constexpr (std::is_same_v<U, std::shared_ptr<Array>>) return "[array]";
                    else if constexpr (std::is_same_v<U, std::shared_ptr<ObjectInstance>>) return "<object>";
                    else return std::to_string(inner);
                }, arg->elements[i]);
            }
            out += "]";
            return out;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Callable>>) {
            return "<fn>";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ObjectInstance>>) {
            return "<" + arg->className + " object>";
        } else {
            return std::to_string(arg);
        }

    }, expr.value);

    return nullptr;
}

Value ASTPrinter::visitVariableExpr(const VariableExpr& expr) {

    result_ = expr.name;
    return nullptr;
}

Value ASTPrinter::visitBinaryExpr(const BinaryExpr& expr) {

    result_ = parenthesize(
        expr.op.lexeme,
        { expr.left.get(), expr.right.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitGroupingExpr(const GroupingExpr& expr) {

    result_ = parenthesize(
        "group",
        { expr.expression.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitUnaryExpr(const UnaryExpr& expr) {

    result_ = parenthesize(
        expr.op.lexeme,
        { expr.right.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitAssignmentExpr(const AssignmentExpr& expr) {

    result_ = parenthesize(
        "assign " + expr.name,
        { expr.value.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitCallExpr(const CallExpr& expr) {

    std::stringstream ss;

    ss << "(call ";
    ss << print(expr.callee.get());

    for (const auto& argument : expr.arguments) {
        ss << " ";
        ss << print(argument.get());
    }

    ss << ")";

    result_ = ss.str();
    return nullptr;
}

Value ASTPrinter::visitArrayExpr(const ArrayExpr& expr) {

    std::stringstream ss;

    ss << "[";

    for (size_t i = 0; i < expr.elements.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << print(expr.elements[i].get());
    }

    ss << "]";

    result_ = ss.str();
    return nullptr;
}

Value ASTPrinter::visitIndexExpr(const IndexExpr& expr) {

    result_ = parenthesize(
        "index",
        { expr.array.get(), expr.index.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitPropertyExpr(const PropertyExpr& expr) {

    result_ = parenthesize(
        "property " + expr.property,
        { expr.object.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {

    result_ = parenthesize(
        "property-assign " + expr.property,
        { expr.object.get(), expr.value.get() }
    );
    return nullptr;
}

Value ASTPrinter::visitThisExpr(const ThisExpr& /*expr*/) {

    result_ = "this";
    return nullptr;
}

Value ASTPrinter::visitIncDecExpr(const IncDecExpr& expr) {
    std::stringstream ss;
    if (expr.isPrefix) {
        ss << "(" << expr.op.lexeme << " " << print(expr.target.get()) << ")";
    } else {
        ss << "(" << print(expr.target.get()) << " " << expr.op.lexeme << ")";
    }
    result_ = ss.str();
    return nullptr;
}

Value ASTPrinter::visitTernaryExpr(const TernaryExpr& expr) {
    result_ = parenthesize(
        "?:",
        { expr.condition.get(), expr.thenBranch.get(), expr.elseBranch.get() }
    );
    return nullptr;
}

// =========================================================================
// StmtVisitor — visit methods
// =========================================================================

void ASTPrinter::visitExprStmt(const ExprStmt& stmt) {

    result_ = print(stmt.expression.get());
}

void ASTPrinter::visitLetStmt(const LetStmt& stmt) {

    std::string label = "let " + stmt.name;
    if (!stmt.typeAnnotation.empty()) {
        label += ":" + stmt.typeAnnotation;
    }

    result_ = parenthesize(
        label,
        { stmt.initializer.get() }
    );
}

void ASTPrinter::visitFuncStmt(const FuncStmt& stmt) {

    std::stringstream ss;

    ss << "(func " << stmt.name;

    for (const auto& param : stmt.params) {
        ss << " " << param;
    }

    ss << " ";

    ss << print(stmt.body.get());

    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitBlockStmt(const BlockStmt& stmt) {

    std::stringstream ss;

    ss << "(block";

    for (const auto& statement : stmt.statements) {
        ss << " ";
        ss << print(statement.get());
    }

    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitReturnStmt(const ReturnStmt& stmt) {

    result_ = parenthesize(
        "return",
        { stmt.value.get() }
    );
}

void ASTPrinter::visitIfStmt(const IfStmt& stmt) {

    std::stringstream ss;

    ss << "(if ";

    ss << print(stmt.condition.get());

    ss << " ";

    ss << print(stmt.thenBranch.get());

    if (stmt.elseBranch) {
        ss << " ";
        ss << print(stmt.elseBranch.get());
    }

    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitWhileStmt(const WhileStmt& stmt) {

    std::stringstream ss;

    ss << "(while ";

    ss << print(stmt.condition.get());

    ss << " ";

    ss << print(stmt.body.get());

    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitForStmt(const ForStmt& stmt) {

    std::stringstream ss;

    ss << "(for ";
    ss << stmt.variable;
    ss << " in ";
    ss << print(stmt.iterable.get());
    ss << " ";
    ss << print(stmt.body.get());
    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitObjStmt(const ObjStmt& stmt) {

    std::stringstream ss;

    ss << "(obj " << stmt.name;

    if (!stmt.parentName.empty()) {
        ss << " : " << stmt.parentName;
    }

    for (const auto& param : stmt.params) {
        ss << " " << param;
    }

    ss << " ";

    ss << print(stmt.body.get());

    for (const auto& method : stmt.methods) {
        ss << " ";
        ss << print(method.get());
    }

    ss << ")";

    result_ = ss.str();
}

void ASTPrinter::visitBreakStmt(const BreakStmt& /*stmt*/) {
    result_ = "(break)";
}

void ASTPrinter::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    result_ = "(continue)";
}

void ASTPrinter::visitTryStmt(const TryStmt& stmt) {
    std::stringstream ss;
    ss << "(try " << print(stmt.tryBlock.get());

    if (stmt.catchBlock) {
        ss << " (catch " << stmt.catchVar << " " << print(stmt.catchBlock.get()) << ")";
    }

    if (stmt.finallyBlock) {
        ss << " (finally " << print(stmt.finallyBlock.get()) << ")";
    }

    ss << ")";
    result_ = ss.str();
}

void ASTPrinter::visitThrowStmt(const ThrowStmt& stmt) {
    result_ = parenthesize("throw", { stmt.value.get() });
}

// =========================================================================
// Program (top-level container — no visitor needed)
// =========================================================================

std::string ASTPrinter::print(const Program* program) {

    std::stringstream ss;

    ss << "(program";

    for (const auto& stmt : program->statements) {
        ss << " ";
        ss << print(stmt.get());
    }

    ss << ")";

    return ss.str();
}

// =========================================================================
// Helper
// =========================================================================

std::string ASTPrinter::parenthesize(
    const std::string& name,
    const std::vector<const Expr*>& exprs
) {

    std::stringstream ss;

    ss << "(" << name;

    for (const auto* expr : exprs) {
        ss << " ";
        ss << print(expr);
    }

    ss << ")";

    return ss.str();
}

}
