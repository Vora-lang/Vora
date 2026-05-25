#pragma once

#include <string>
#include <vector>

#include "expr.h"

namespace vora {

class ASTPrinter {
public:
    std::string print(const Expr* expr);

private:
    std::string parenthesize(
        const std::string& name,
        const std::vector<const Expr*>& exprs
    );
};

}
