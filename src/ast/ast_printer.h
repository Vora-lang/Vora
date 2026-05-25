#pragma once

#include <string>

#include "expr.h"

namespace vora {

class ASTPrinter {
public:
    std::string print(const Expr* expr);

private:
    std::string parenthesize(
        const std::string& name,
        const Expr* left,
        const Expr* right
    );
};

}
