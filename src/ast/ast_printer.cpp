#include "ast_printer.h"

#include <sstream>

namespace vora {

std::string ASTPrinter::print(const Expr* expr) {

    if (auto literal =
        dynamic_cast<const LiteralExpr*>(expr)) {

        return literal->value;
    }

    if (auto identifier =
        dynamic_cast<const IdentifierExpr*>(expr)) {

        return identifier->name;
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
