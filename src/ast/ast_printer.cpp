#include "ast_printer.h"

#include <sstream>

namespace vora {

std::string ASTPrinter::print(const Expr* expr) {

    if (auto literal =
        dynamic_cast<const LiteralExpr*>(expr)) {

        return literal->value;
    }

    if (auto binary =
        dynamic_cast<const BinaryExpr*>(expr)) {

        return parenthesize(
            binary->op.lexeme,
            binary->left.get(),
            binary->right.get()
        );
    }

    return "unknown";
}

std::string ASTPrinter::parenthesize(
    const std::string& name,
    const Expr* left,
    const Expr* right
) {

    std::stringstream ss;

    ss << "(" << name << " ";

    ss << print(left);

    ss << " ";

    ss << print(right);

    ss << ")";

    return ss.str();
}

}
