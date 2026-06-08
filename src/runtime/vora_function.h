#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../ast/stmt.h"
#include "callable.h"

namespace vora {

class Environment;
class Interpreter;
struct FunctionPrototype;

class VoraFunction : public Callable {
public:
    // Interpreter-mode constructor (tree-walking)
    VoraFunction(
        std::string name,
        std::vector<std::string> params,
        std::shared_ptr<BlockStmt> body,
        std::shared_ptr<Environment> closure
    );

    // VM-mode constructor (compiled bytecode)
    VoraFunction(
        std::string name,
        int arity,
        const FunctionPrototype* prototype
    );

    Value call(
        Interpreter& interpreter,
        const std::vector<Value>& arguments
    ) override;

    const std::string& name() const;

    const std::vector<std::string>& params() const;

    int arity() const { return static_cast<int>(arity_); }

    const std::shared_ptr<BlockStmt>& body() const;

    const std::shared_ptr<Environment>& closure() const;

    const FunctionPrototype* getPrototype() const { return prototype_; }

    bool isCompiled() const { return prototype_ != nullptr; }

    // Upvalues for VM closures (shared heap-allocated values)
    std::vector<std::shared_ptr<Value>> upvalues;

private:
    std::string name_;

    std::vector<std::string> params_;

    int arity_ = 0;

    std::shared_ptr<BlockStmt> body_;

    std::shared_ptr<Environment> closure_;

    const FunctionPrototype* prototype_ = nullptr;
};

}
