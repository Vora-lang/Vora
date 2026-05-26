#pragma once

#include <memory>
#include <vector>

#include "stmt.h"

namespace vora {

class Program {
public:
    explicit Program(
        std::vector<std::unique_ptr<Stmt>> statements
    )
        : statements(std::move(statements)) {
    }

    std::vector<std::unique_ptr<Stmt>> statements;
};

}
