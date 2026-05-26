#include "environment.h"

#include <cstddef>
#include <iostream>

namespace vora {

void Environment::define(
    const std::string& name,
    const Value& value
) {

    values[name] = value;
}

Value Environment::get(
    const std::string& name
) {

    auto it = values.find(name);

    if (it == values.end()) {

        std::cerr
            << "Undefined variable: "
            << name
            << std::endl;

        return nullptr;
    }

    return it->second;
}

void Environment::assign(
    const std::string& name,
    const Value& value
) {

    auto it = values.find(name);

    if (it == values.end()) {

        std::cerr
            << "Undefined variable: "
            << name
            << std::endl;

        return;
    }

    it->second = value;
}

}
