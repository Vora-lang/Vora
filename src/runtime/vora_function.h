#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../ast/stmt.h"
#include "callable.h"

namespace vora {

class Environment;
class Interpreter;

class VoraFunction : public Callable {
public:
    VoraFunction(
        std::string name,
        std::vector<std::string> params,
        std::shared_ptr<BlockStmt> body,
        std::shared_ptr<Environment> closure
    );

    Value call(
        Interpreter& interpreter,
        const std::vector<Value>& arguments
    ) override;

    const std::string& name() const;

    const std::vector<std::string>& params() const;

    const std::shared_ptr<BlockStmt>& body() const;

    const std::shared_ptr<Environment>& closure() const;

private:
    std::string name_;

    std::vector<std::string> params_;

    std::shared_ptr<BlockStmt> body_;

    std::shared_ptr<Environment> closure_;
};

}
