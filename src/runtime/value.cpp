#include "value.h"

#include "callable.h"
#include "native_function.h"
#include "vora_function.h"
#include "../vm/compiler.h"

#include <iostream>

namespace vora {

// =========================================================================
// pushGcRefs — defined here because it needs complete types (Callable : GcObject)
// =========================================================================

void pushGcRefs(const Value& v, std::vector<GcObject*>& worklist) {
    if (auto* p = std::get_if<GcPtr<GcString>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<Array>>(&v)) {
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
    } else if (auto* p = std::get_if<GcPtr<Iterator>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<Generator>>(&v)) {
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

void Iterator::trace(std::vector<GcObject*>& wl) {
    pushGcRefs(source, wl);
}

void Generator::trace(std::vector<GcObject*>& wl) {
    if (function) wl.push_back(function.get());
    for (auto& v : args) pushGcRefs(v, wl);
    for (auto& v : savedStack) pushGcRefs(v, wl);
}

// =========================================================================
// valueToStringAppend — internal depth-limited streaming worker
// =========================================================================

static constexpr int kMaxPrintDepth = 256;

// Forward declaration for recursion within same TU
static void appendValue(std::string& out, const Value& value, int depth);

static void appendValue(std::string& out, const Value& value, int depth) {
    if (depth > kMaxPrintDepth) {
        out += "[max depth]";
        return;
    }

    std::visit([&out, depth](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            out += "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            out += (arg ? "true" : "false");
        } else if constexpr (std::is_same_v<T, GcPtr<GcString>>) {
            out += arg->value;
        } else if constexpr (std::is_same_v<T, GcPtr<Array>>) {
            out += '[';
            for (size_t i = 0; i < arg->elements.size(); ++i) {
                if (i > 0) out += ", ";
                appendValue(out, arg->elements[i], depth + 1);
            }
            out += ']';
        } else if constexpr (std::is_same_v<T, GcPtr<Dict>>) {
            out += '{';
            size_t i = 0;
            for (const auto& [k, v] : arg->pairs) {
                if (i > 0) out += ", ";
                out += k;
                out += ": ";
                appendValue(out, v, depth + 1);
                i++;
            }
            out += '}';
        } else if constexpr (std::is_same_v<T, GcPtr<Callable>>) {
            if (auto native = dynamic_cast<NativeFunction*>(arg.get())) {
                out += "<native fn ";
                out += native->name();
                out += '>';
            } else if (auto fn = dynamic_cast<VoraFunction*>(arg.get())) {
                out += "<fn ";
                out += fn->name();
                out += '>';
            } else {
                out += "<fn>";
            }
        } else if constexpr (std::is_same_v<T, GcPtr<ObjectInstance>>) {
            out += '<';
            out += arg->className;
            out += " object>";
        } else if constexpr (std::is_same_v<T, GcPtr<Iterator>>) {
            out += "<iterator>";
        } else if constexpr (std::is_same_v<T, GcPtr<Generator>>) {
            out += "<generator ";
            out += (arg->function ? arg->function->name() : "?");
            out += '>';
        } else if constexpr (std::is_same_v<T, GcPtr<FunctionPrototype>>) {
            out += "<function prototype ";
            out += arg->name;
            out += '>';
        } else if constexpr (std::is_same_v<T, GcPtr<ClassDefinition>>) {
            out += "<class ";
            out += arg->name;
            out += '>';
        } else if constexpr (std::is_same_v<T, int64_t>) {
            out += std::to_string(arg);
        } else {
            // double
            out += std::to_string(arg);
        }
    }, value);
}

void valueToStringAppend(std::string& out, const Value& value, int depth) {
    appendValue(out, value, depth);
}

// =========================================================================
// printValue
// =========================================================================

void printValue(const Value& value) {
    std::string s = valueToString(value);
    std::cout << s;
}

// =========================================================================
// valueToString
// =========================================================================

std::string valueToString(const Value& value) {
    std::string out;
    appendValue(out, value, 0);
    return out;
}

}
