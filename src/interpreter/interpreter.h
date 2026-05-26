#pragma once

#include "../ast/expr.h"
#include "../ast/stmt.h"

#include "../runtime/environment.h"
#include "../runtime/value.h"
#include "../ast/program.h"

namespace vora {

class Interpreter {
public:

    void interpret(
        const Program* program
    );

private:

    Environment environment;

    Value evaluate(
        const Expr* expr
    );

    void execute(
        const Stmt* stmt
    );
};

}
