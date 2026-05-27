#include "value.h"

#include "callable.h"
#include "native_function.h"
#include "vora_function.h"

#include <iostream>

namespace vora {

void printValue(const Value& value) {

    std::visit([](auto&& arg) {

        using T =
            std::decay_t<decltype(arg)>;

        if constexpr (
            std::is_same_v<T, std::nullptr_t>
        ) {

            std::cout << "null";

        } else if constexpr (
            std::is_same_v<T, bool>
        ) {

            std::cout
                << (arg ? "true" : "false");

        } else if constexpr (
            std::is_same_v<T, std::shared_ptr<Callable>>
        ) {

            if (auto native =
                std::dynamic_pointer_cast<NativeFunction>(arg)) {

                std::cout
                    << "<native fn "
                    << native->name()
                    << ">";

            } else if (auto function =
                std::dynamic_pointer_cast<VoraFunction>(arg)) {

                std::cout
                    << "<fn "
                    << function->name()
                    << ">";

            } else {

                std::cout << "<fn>";
            }

        } else {

            std::cout << arg;
        }

    }, value);
}

}
