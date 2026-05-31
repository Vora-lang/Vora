#pragma once

#include <memory>
#include <vector>

#include "stmt.h"

namespace vora {

template <typename R>
class ProgramVisitor {
public:
    using ReturnType = R;

    virtual ~ProgramVisitor() = default;
    virtual R visitProgram(const class Program& program) = 0;
};

class Program {
public:
    explicit Program(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    // Visitor dispatch. Templated because Program has no subclasses, so we
    // don't need a virtual accept() — the template instantiates correctly
    // for any R the caller uses.
    template <typename R>
    R accept(ProgramVisitor<R>& visitor) const {
        return visitor.visitProgram(*this);
    }

    std::vector<std::unique_ptr<Stmt>> statements;
};

}
