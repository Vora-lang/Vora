#include "value.h"

#include "callable.h"
#include "native_function.h"
#include "vora_function.h"
#include "../vm/compiler.h"

#include <iostream>
#include <sstream>

namespace vora {

// =========================================================================
// pushGcRefs — defined here because it needs complete types (Callable : GcObject)
// =========================================================================

void pushGcRefs(const Value& v, std::vector<GcObject*>& worklist) {
    if (auto* p = std::get_if<GcPtr<Array>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<Dict>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<Callable>>(&v)) {
        if (*p) worklist.push_back(static_cast<GcObject*>(p->get()));
    } else if (auto* p = std::get_if<GcPtr<ObjectInstance>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<FunctionPrototype>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<ClassDefinition>>(&v)) {
        if (*p) worklist.push_back(p->get());
    }
}

// =========================================================================
// trace() implementations
// =========================================================================

void Array::trace(std::vector<GcObject*>& wl) {
    for (auto& e : elements) pushGcRefs(e, wl);
}

void Dict::trace(std::vector<GcObject*>& wl) {
    for (auto& [k, v] : pairs) pushGcRefs(v, wl);
}

void ClassDefinition::trace(std::vector<GcObject*>& wl) {
    if (ctorProto) wl.push_back(ctorProto.get());
    for (auto& mp : methodProtos) {
        if (mp) wl.push_back(mp.get());
    }
    for (auto& [name, fn] : methods) {
        if (fn) wl.push_back(fn.get());
    }
    for (auto& pc : parentClasses) {
        if (pc) wl.push_back(pc.get());
    }
    for (auto& m : mro) {
        if (m) wl.push_back(m.get());
    }
}

void ObjectInstance::trace(std::vector<GcObject*>& wl) {
    if (classDefinition) wl.push_back(classDefinition.get());
    for (auto& [k, v] : properties) pushGcRefs(v, wl);
}

// =========================================================================
// printValue
// =========================================================================

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
            std::is_same_v<T, GcPtr<Array>>
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
            std::is_same_v<T, GcPtr<Dict>>
        ) {
            std::cout << "{";
            size_t i = 0;
            for (const auto& [k, v] : arg->pairs) {
                if (i > 0) std::cout << ", ";
                std::cout << k << ": ";
                printValue(v);
                i++;
            }
            std::cout << "}";
        } else if constexpr (
            std::is_same_v<T, GcPtr<Callable>>
        ) {

            if (auto native =
                dynamic_cast<NativeFunction*>(arg.get())) {

                std::cout
                    << "<native fn "
                    << native->name()
                    << ">";

            } else if (auto function =
                dynamic_cast<VoraFunction*>(arg.get())) {

                std::cout
                    << "<fn "
                    << function->name()
                    << ">";

            } else {

                std::cout << "<fn>";
            }

        } else if constexpr (
            std::is_same_v<T, GcPtr<ObjectInstance>>
        ) {

            std::cout
                << "<"
                << arg->className
                << " object>";

        } else if constexpr (
            std::is_same_v<T, GcPtr<FunctionPrototype>>
        ) {

            std::cout
                << "<function prototype "
                << arg->name
                << ">";

        } else if constexpr (
            std::is_same_v<T, GcPtr<ClassDefinition>>
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
        } else if constexpr (std::is_same_v<T, GcPtr<Array>>) {
            oss << "[";
            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << valueToString(arg->elements[i]);
            }
            oss << "]";
        } else if constexpr (std::is_same_v<T, GcPtr<Dict>>) {
            oss << "{";
            size_t i = 0;
            for (const auto& [k, v] : arg->pairs) {
                if (i > 0) oss << ", ";
                oss << k << ": " << valueToString(v);
                i++;
            }
            oss << "}";
        } else if constexpr (std::is_same_v<T, GcPtr<Callable>>) {
            if (auto native = dynamic_cast<NativeFunction*>(arg.get())) {
                oss << "<native fn " << native->name() << ">";
            } else if (auto fn = dynamic_cast<VoraFunction*>(arg.get())) {
                oss << "<fn " << fn->name() << ">";
            } else {
                oss << "<fn>";
            }
        } else if constexpr (std::is_same_v<T, GcPtr<ObjectInstance>>) {
            oss << "<" << arg->className << " object>";
        } else if constexpr (std::is_same_v<T, GcPtr<FunctionPrototype>>) {
            oss << "<function prototype " << arg->name << ">";
        } else if constexpr (std::is_same_v<T, GcPtr<ClassDefinition>>) {
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
