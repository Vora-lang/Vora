#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "callable.h"
#include "value.h"

namespace vora {

class ObjectConstructor : public Callable {
public:
    ObjectConstructor(
        std::shared_ptr<ObjectClass> classDefinition
    );

    Value call(
        Interpreter& interpreter,
        const std::vector<Value>& arguments
    ) override;

private:
    std::shared_ptr<ObjectClass> classDefinition_;
};

}
