#include "ast_printer.h"
#include "program.h"

#include "../runtime/callable.h"

#include <sstream>

namespace vora {

std::string ASTPrinter::print(const Expr* expr) {

    if (auto literal =
        dynamic_cast<const LiteralExpr*>(expr)) {

        return std::visit([](auto&& arg) -> std::string {

            using T =
                std::decay_t<decltype(arg)>;

            if constexpr (
                std::is_same_v<T, std::nullptr_t>
                ) {

                return "null";

            } else if constexpr (
                std::is_same_v<T, bool>
                ) {

                return arg ? "true" : "false";

            } else if constexpr (
                std::is_same_v<T, std::string>
                ) {

                return arg;

            } else if constexpr (
                std::is_same_v<T, std::shared_ptr<Array>>
                ) {

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
                        else return std::to_string(inner);
                    }, arg->elements[i]);
                }
                out += "]";
                return out;

            } else if constexpr (
                std::is_same_v<T, std::shared_ptr<Callable>>
                ) {

                return "<fn>";

            } else {

                return std::to_string(arg);
            }

        }, literal->value);
    }

    if (auto variable =
        dynamic_cast<const VariableExpr*>(expr)) {

        return variable->name;
    }

    if (auto binary =
        dynamic_cast<const BinaryExpr*>(expr)) {

        return parenthesize(
            binary->op.lexeme,
            {
                binary->left.get(),
                binary->right.get()
            }
        );
    }

    if (auto grouping =
        dynamic_cast<const GroupingExpr*>(expr)) {

        return parenthesize(
            "group",
            {
                grouping->expression.get()
            }
        );
    }

    if (auto unary =
        dynamic_cast<const UnaryExpr*>(expr)) {

        return parenthesize(
            unary->op.lexeme,
            {
                unary->right.get()
            }
        );
    }

    if (auto assign =
        dynamic_cast<const AssignmentExpr*>(expr)) {

        return parenthesize(
            "assign " + assign->name,
            {
                assign->value.get()
            }
        );
    }

    if (auto call =
        dynamic_cast<const CallExpr*>(expr)) {

        std::stringstream ss;

        ss << "(call ";
        ss << print(call->callee.get());

        for (const auto& argument : call->arguments) {
            ss << " ";
            ss << print(argument.get());
        }

        ss << ")";

        return ss.str();
    }

    if (auto array =
        dynamic_cast<const ArrayExpr*>(expr)) {

        std::stringstream ss;

        ss << "[";

        for (size_t i = 0; i < array->elements.size(); ++i) {
            if (i > 0) {
                ss << ", ";
            }
            ss << print(array->elements[i].get());
        }

        ss << "]";

        return ss.str();
    }

    if (auto indexExpr =
        dynamic_cast<const IndexExpr*>(expr)) {

        return parenthesize(
            "index",
            {
                indexExpr->array.get(),
                indexExpr->index.get()
            }
        );
    }

    return "unknown";
}

std::string ASTPrinter::print(const Stmt* stmt) {

    if (auto exprStmt =
        dynamic_cast<const ExprStmt*>(stmt)) {

        return print(
            exprStmt->expression.get()
        );
    }

    if (auto letStmt =
        dynamic_cast<const LetStmt*>(stmt)) {

        return parenthesize(
            "let " + letStmt->name,
            {
                letStmt->initializer.get()
            }
        );
    }

    if (auto func =
        dynamic_cast<const FuncStmt*>(stmt)) {

        std::stringstream ss;

        ss << "(func " << func->name;

        for (const auto& param : func->params) {
            ss << " " << param;
        }

        ss << " ";

        ss << print(func->body.get());

        ss << ")";

        return ss.str();
    }

    if (auto block =
        dynamic_cast<const BlockStmt*>(stmt)) {

        std::stringstream ss;

        ss << "(block";

        for (const auto& statement :
             block->statements) {

            ss << " ";

            ss << print(statement.get());
        }

        ss << ")";

        return ss.str();
    }

    if (auto returnStmt =
        dynamic_cast<const ReturnStmt*>(stmt)) {

        return parenthesize(
            "return",
            {
                returnStmt->value.get()
            }
        );
    }

    if (auto ifStmt =
        dynamic_cast<const IfStmt*>(stmt)) {

        std::stringstream ss;

        ss << "(if ";

        ss << print(ifStmt->condition.get());

        ss << " ";

        ss << print(ifStmt->thenBranch.get());

        if (ifStmt->elseBranch) {

            ss << " ";

            ss << print(ifStmt->elseBranch.get());
        }

        ss << ")";

        return ss.str();
    }

    if (auto whileStmt =
        dynamic_cast<const WhileStmt*>(stmt)) {

        std::stringstream ss;

        ss << "(while ";

        ss << print(
            whileStmt->condition.get()
        );

        ss << " ";

        ss << print(
            whileStmt->body.get()
        );

        ss << ")";

        return ss.str();
    }

    return "unknown stmt";
}

std::string ASTPrinter::print(const Program* program) {

    std::stringstream ss;

    ss << "(program";

    for (const auto& stmt :
         program->statements) {

        ss << " ";

        ss << print(stmt.get());
    }

    ss << ")";

    return ss.str();
}

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
