#include "value.h"

#include "callable.h"
#include "native_function.h"
#include "vora_function.h"
#include "../vm/compiler.h"

#include <iostream>
#include <sstream>

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
            std::is_same_v<T, std::shared_ptr<Array>>
        ) {

            std::cout << "[";

            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }
                printValue(arg->elements[i]);
            }

            std::cout << "]";

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

        } else if constexpr (
            std::is_same_v<T, std::shared_ptr<ObjectInstance>>
        ) {

            std::cout
                << "<"
                << arg->className
                << " object>";

        } else if constexpr (
            std::is_same_v<T, std::shared_ptr<FunctionPrototype>>
        ) {

            std::cout
                << "<function prototype "
                << arg->name
                << ">";

        } else if constexpr (
            std::is_same_v<T, std::shared_ptr<ClassData>>
        ) {

            std::cout
                << "<class "
                << arg->name
                << ">";

        } else if constexpr (
            std::is_same_v<T, int64_t>
        ) {

            std::cout << arg;

        } else {

            std::cout << arg;
        }

    }, value);
}

std::string valueToString(const Value& value) {

    std::ostringstream oss;

    std::visit([&oss](auto&& arg) {

        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            oss << "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            oss << (arg ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) {
            oss << "[";
            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << valueToString(arg->elements[i]);
            }
            oss << "]";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Callable>>) {
            if (auto native = std::dynamic_pointer_cast<NativeFunction>(arg)) {
                oss << "<native fn " << native->name() << ">";
            } else if (auto fn = std::dynamic_pointer_cast<VoraFunction>(arg)) {
                oss << "<fn " << fn->name() << ">";
            } else {
                oss << "<fn>";
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ObjectInstance>>) {
            oss << "<" << arg->className << " object>";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionPrototype>>) {
            oss << "<function prototype " << arg->name << ">";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ClassData>>) {
            oss << "<class " << arg->name << ">";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            oss << arg;
        } else {
            // double, string
            oss << arg;
        }

    }, value);

    return oss.str();
}

}
