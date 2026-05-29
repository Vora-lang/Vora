#include "object_constructor.h"
#include "environment.h"
#include "../interpreter/interpreter.h"
#include "../ast/stmt.h"

namespace vora {

ObjectConstructor::ObjectConstructor(
    std::shared_ptr<ObjectClass> classDefinition
)
    : classDefinition_(std::move(classDefinition)) {
}

Value ObjectConstructor::call(
    Interpreter& interpreter,
    const std::vector<Value>& arguments
) {
    auto instance = std::make_shared<ObjectInstance>();
    instance->className = classDefinition_->className;
    instance->classDefinition = classDefinition_;

    VoraFunction constructor(
        classDefinition_->className,
        classDefinition_->params,
        classDefinition_->body,
        nullptr
    );

    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i < constructor.params().size()) {
            interpreter.getEnvironment().define(
                constructor.params()[i],
                arguments[i]
            );
        }
    }

    interpreter.getEnvironment().define(
        "this",
        instance
    );

    try {
        interpreter.executeBlock(
            constructor.body()->statements
        );
    } catch (...) {
        throw;
    }

    return instance;
}

}
