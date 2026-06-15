#include "ast_printer.h"
#include "program.h"

#include "../runtime/callable.h"
#include "../vm/compiler.h"

#include <sstream>

namespace vora {

// =========================================================================
// Entry points — delegate via double dispatch
// =========================================================================

std::string ASTPrinter::print(const Expr* expr) {
    return expr->accept(*this);
}

std::string ASTPrinter::print(const Stmt* stmt) {
    return stmt->accept(*this);
}

std::string ASTPrinter::print(const Program* program) {
    return program->accept(*this);
}

// =========================================================================
// ExprVisitor<std::string> — visit methods return strings directly
// =========================================================================

std::string ASTPrinter::visitLiteralExpr(const LiteralExpr& expr) {

    return std::visit([](auto&& arg) -> std::string {

        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, GcPtr<GcString>>) {
            return arg->value;
        } else if constexpr (std::is_same_v<T, GcPtr<Array>>) {
            std::string out = "[";
            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += std::visit([](auto&& inner) -> std::string {
                    using U = std::decay_t<decltype(inner)>;
                    if constexpr (std::is_same_v<U, std::nullptr_t>) return "null";
                    else if constexpr (std::is_same_v<U, bool>) return inner ? "true" : "false";
                    else if constexpr (std::is_same_v<U, GcPtr<GcString>>) return inner->value;
                    else if constexpr (std::is_same_v<U, GcPtr<Callable>>) return "<fn>";
                    else if constexpr (std::is_same_v<U, GcPtr<Array>>) return "[array]";
                    else if constexpr (std::is_same_v<U, GcPtr<ObjectInstance>>) return "<object>";
                    else if constexpr (std::is_same_v<U, GcPtr<FunctionPrototype>>) return "<proto>";
                    else if constexpr (std::is_same_v<U, GcPtr<Dict>>) return "{dict}";
                    else if constexpr (std::is_same_v<U, GcPtr<ClassDefinition>>) return "<class>";
                    else return std::to_string(inner);
                }, arg->elements[i]);
            }
            out += "]";
            return out;
        } else if constexpr (std::is_same_v<T, GcPtr<Callable>>) {
            return "<fn>";
        } else if constexpr (std::is_same_v<T, GcPtr<ObjectInstance>>) {
            return "<" + arg->className + " object>";
        } else if constexpr (std::is_same_v<T, GcPtr<FunctionPrototype>>) {
            return "<proto " + arg->name + ">";
        } else if constexpr (std::is_same_v<T, GcPtr<Dict>>) {
            return "{dict}";
        } else if constexpr (std::is_same_v<T, GcPtr<ClassDefinition>>) {
            return "<class " + arg->name + ">";
        } else {
            return std::to_string(arg);
        }

    }, expr.value);
}

std::string ASTPrinter::visitVariableExpr(const VariableExpr& expr) {
    return expr.name;
}

std::string ASTPrinter::visitBinaryExpr(const BinaryExpr& expr) {
    return parenthesize(
        expr.op.lexeme,
        { expr.left.get(), expr.right.get() }
    );
}

std::string ASTPrinter::visitGroupingExpr(const GroupingExpr& expr) {
    return parenthesize(
        "group",
        { expr.expression.get() }
    );
}

std::string ASTPrinter::visitUnaryExpr(const UnaryExpr& expr) {
    return parenthesize(
        expr.op.lexeme,
        { expr.right.get() }
    );
}

std::string ASTPrinter::visitAssignmentExpr(const AssignmentExpr& expr) {
    return parenthesize(
        "assign " + expr.name,
        { expr.value.get() }
    );
}

std::string ASTPrinter::visitCompoundAssignmentExpr(const CompoundAssignmentExpr& expr) {
    std::stringstream ss;
    ss << "(" << expr.op.lexeme << " ";
    ss << print(expr.target.get()) << " ";
    ss << print(expr.value.get()) << ")";
    return ss.str();
}

std::string ASTPrinter::visitCallExpr(const CallExpr& expr) {
    std::stringstream ss;

    ss << "(call ";
    ss << print(expr.callee.get());

    for (const auto& argument : expr.arguments) {
        ss << " ";
        ss << print(argument.get());
    }

    ss << ")";
    return ss.str();
}

std::string ASTPrinter::visitArrayExpr(const ArrayExpr& expr) {
    std::stringstream ss;

    ss << "[";

    for (size_t i = 0; i < expr.elements.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << print(expr.elements[i].get());
    }

    ss << "]";
    return ss.str();
}

std::string ASTPrinter::visitDictExpr(const DictExpr& expr) {
    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < expr.pairs.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << print(expr.pairs[i].first.get()) << ": "
           << print(expr.pairs[i].second.get());
    }
    ss << "}";
    return ss.str();
}

std::string ASTPrinter::visitIndexExpr(const IndexExpr& expr) {
    return parenthesize(
        "index",
        { expr.array.get(), expr.index.get() }
    );
}

std::string ASTPrinter::visitPropertyExpr(const PropertyExpr& expr) {
    return parenthesize(
        "property " + expr.property,
        { expr.object.get() }
    );
}

std::string ASTPrinter::visitPropertyAssignmentExpr(const PropertyAssignmentExpr& expr) {
    return parenthesize(
        "property-assign " + expr.property,
        { expr.object.get(), expr.value.get() }
    );
}

std::string ASTPrinter::visitIndexAssignmentExpr(const IndexAssignmentExpr& expr) {
    return parenthesize(
        "index-assign",
        { expr.object.get(), expr.index.get(), expr.value.get() }
    );
}

std::string ASTPrinter::visitThisExpr(const ThisExpr& /*expr*/) {
    return "this";
}

std::string ASTPrinter::visitSuperExpr(const SuperExpr& /*expr*/) {
    return "super";
}

std::string ASTPrinter::visitIncDecExpr(const IncDecExpr& expr) {
    std::stringstream ss;
    if (expr.isPrefix) {
        ss << "(" << expr.op.lexeme << " " << print(expr.target.get()) << ")";
    } else {
        ss << "(" << print(expr.target.get()) << " " << expr.op.lexeme << ")";
    }
    return ss.str();
}

std::string ASTPrinter::visitTernaryExpr(const TernaryExpr& expr) {
    return parenthesize(
        "?:",
        { expr.condition.get(), expr.thenBranch.get(), expr.elseBranch.get() }
    );
}

std::string ASTPrinter::visitFuncExpr(const FuncExpr& expr) {
    std::stringstream ss;
    ss << "(func";
    for (const auto& param : expr.params) {
        if (param.defaultValue) {
            ss << " (" << param.name << " " << print(param.defaultValue.get()) << ")";
        } else {
            ss << " " << param.name;
        }
    }
    ss << " (block";
    for (const auto& s : expr.body->statements) {
        ss << " " << print(s.get());
    }
    ss << "))";
    return ss.str();
}

// =========================================================================
// StmtVisitor<std::string> — visit methods return strings directly
// =========================================================================

std::string ASTPrinter::visitExprStmt(const ExprStmt& stmt) {
    return print(stmt.expression.get());
}

std::string ASTPrinter::visitLetStmt(const LetStmt& stmt) {
    std::string label = (stmt.isConst ? "const " : "let ") + stmt.name;
    if (!stmt.typeAnnotation.empty()) {
        label += ":" + stmt.typeAnnotation;
    }
    return parenthesize(
        label,
        { stmt.initializer.get() }
    );
}

std::string ASTPrinter::visitFuncStmt(const FuncStmt& stmt) {
    std::stringstream ss;

    ss << "(func " << stmt.name;

    for (const auto& param : stmt.params) {
        if (param.defaultValue) {
            ss << " (" << param.name << " " << print(param.defaultValue.get()) << ")";
        } else {
            ss << " " << param.name;
        }
    }

    ss << " ";
    ss << print(stmt.body.get());
    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitBlockStmt(const BlockStmt& stmt) {
    std::stringstream ss;

    ss << "(block";

    for (const auto& statement : stmt.statements) {
        ss << " ";
        ss << print(statement.get());
    }

    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitReturnStmt(const ReturnStmt& stmt) {
    return parenthesize(
        "return",
        { stmt.value.get() }
    );
}

std::string ASTPrinter::visitIfStmt(const IfStmt& stmt) {
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

    return ss.str();
}

std::string ASTPrinter::visitWhileStmt(const WhileStmt& stmt) {
    std::stringstream ss;

    ss << "(while ";
    ss << print(stmt.condition.get());
    ss << " ";
    ss << print(stmt.body.get());
    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitForStmt(const ForStmt& stmt) {
    std::stringstream ss;

    ss << "(for ";
    ss << stmt.variable;
    ss << " in ";
    ss << print(stmt.iterable.get());
    ss << " ";
    ss << print(stmt.body.get());
    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitCForStmt(const CForStmt& stmt) {
    std::stringstream ss;

    ss << "(cfor ";

    if (stmt.initializer) {
        ss << print(stmt.initializer.get());
    } else {
        ss << "nil";
    }
    ss << " ";

    if (stmt.condition) {
        ss << print(stmt.condition.get());
    } else {
        ss << "true";
    }
    ss << " ";

    if (stmt.increment) {
        ss << print(stmt.increment.get());
    } else {
        ss << "nil";
    }
    ss << " ";
    ss << print(stmt.body.get());
    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitObjStmt(const ObjStmt& stmt) {
    std::stringstream ss;

    ss << "(obj " << stmt.name;

    if (!stmt.parentNames.empty()) {
        ss << " :";
        for (const auto& pn : stmt.parentNames) {
            ss << " " << pn;
        }
    }

    for (const auto& param : stmt.params) {
        if (param.defaultValue) {
            ss << " (" << param.name << " " << print(param.defaultValue.get()) << ")";
        } else {
            ss << " " << param.name;
        }
    }

    ss << " ";
    ss << print(stmt.body.get());

    for (const auto& method : stmt.methods) {
        ss << " ";
        ss << print(method.get());
    }

    ss << ")";

    return ss.str();
}

std::string ASTPrinter::visitBreakStmt(const BreakStmt& /*stmt*/) {
    return "(break)";
}

std::string ASTPrinter::visitContinueStmt(const ContinueStmt& /*stmt*/) {
    return "(continue)";
}

std::string ASTPrinter::visitTryStmt(const TryStmt& stmt) {
    std::stringstream ss;
    ss << "(try " << print(stmt.tryBlock.get());

    if (stmt.catchBlock) {
        ss << " (catch " << stmt.catchVar << " " << print(stmt.catchBlock.get()) << ")";
    }

    if (stmt.finallyBlock) {
        ss << " (finally " << print(stmt.finallyBlock.get()) << ")";
    }

    ss << ")";
    return ss.str();
}

std::string ASTPrinter::visitThrowStmt(const ThrowStmt& stmt) {
    return parenthesize("throw", { stmt.value.get() });
}

// =========================================================================
// ProgramVisitor<std::string>
// =========================================================================

std::string ASTPrinter::visitProgram(const Program& program) {
    std::stringstream ss;

    ss << "(program";

    for (const auto& stmt : program.statements) {
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
