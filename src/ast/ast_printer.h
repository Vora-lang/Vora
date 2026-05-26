#pragma once

#include <string>
#include <vector>

#include "expr.h"
#include "stmt.h"
#include "program.h"

namespace vora {

class ASTPrinter {
public:
    std::string print(const Expr* expr);
    std::string print(const Stmt* stmt);
    std::string print(const Program* program);

private:
    std::string parenthesize(
        const std::string& name,
        const std::vector<const Expr*>& exprs
    );
};

}
