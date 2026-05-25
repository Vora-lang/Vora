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
