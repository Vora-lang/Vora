#include "value.h"

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

            } else {

                std::cout << arg;
            }

        }, value);
    }
}
