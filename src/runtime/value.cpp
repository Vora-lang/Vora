#include "value.h"

#include "callable.h"
#include "native_function.h"
#include "vora_function.h"
#include "../vm/compiler.h"

#include <algorithm>
#include <iostream>

namespace vora {

// =========================================================================
// ValueHash — content-based hash for Value (used by Set/Map)
// =========================================================================

// Simple hash combiner (boost::hash_combine equivalent)
static inline void hashCombine(size_t& seed, size_t h) {
    seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t ValueHash::operator()(const Value& v) const {
    return std::visit([](auto&& arg) -> size_t {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return std::hash<bool>{}(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::hash<int64_t>{}(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            // Normalize -0.0 to 0.0, and integer-valued doubles to int64
            double d = (arg == -0.0) ? 0.0 : arg;
            if (d == static_cast<double>(static_cast<int64_t>(d))
                && d >= static_cast<double>(INT64_MIN)
                && d < static_cast<double>(INT64_MAX)) {
                return std::hash<int64_t>{}(static_cast<int64_t>(d));
            }
            return std::hash<double>{}(d);
        } else if constexpr (std::is_same_v<T, GcPtr<GcString>>) {
            return std::hash<std::string>{}(arg->value);
        } else if constexpr (std::is_same_v<T, GcPtr<Array>>) {
            size_t h = 0;
            for (const auto& e : arg->elements) {
                hashCombine(h, ValueHash{}(e));
            }
            return h;
        } else if constexpr (std::is_same_v<T, GcPtr<Dict>>) {
            // Dict iteration order is non-deterministic; sort keys for determinism
            std::vector<std::string> keys;
            keys.reserve(arg->pairs.size());
            for (const auto& [k, v] : arg->pairs) keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            size_t h = 0;
            for (const auto& k : keys) {
                hashCombine(h, std::hash<std::string>{}(k));
                hashCombine(h, ValueHash{}(arg->pairs.at(k)));
            }
            return h;
        } else if constexpr (std::is_same_v<T, GcPtr<Set>>) {
            // Order-independent: XOR individual hashes
            size_t h = 0;
            for (const auto& e : arg->elements) {
                h ^= ValueHash{}(e);
            }
            return h;
        } else if constexpr (std::is_same_v<T, GcPtr<Map>>) {
            // Order-independent XOR of (keyHash ^ valueHash)
            size_t h = 0;
            for (const auto& [k, v] : arg->pairs) {
                h ^= (ValueHash{}(k) ^ ValueHash{}(v));
            }
            return h;
        } else {
            // Identity hash for functions, objects, classes, iterators, generators
            // (GcPtr stores a raw pointer)
            if constexpr (std::is_pointer_v<T>) {
                return std::hash<const void*>{}(static_cast<const void*>(arg));
            } else {
                // GcPtr<T> — extract the raw pointer
                return std::hash<const void*>{}(static_cast<const void*>(arg.get()));
            }
        }
    }, v);
}

// =========================================================================
// ValueEqual — deep structural equality for Value (used by Set/Map)
// =========================================================================

bool ValueEqual::operator()(const Value& a, const Value& b) const {
    // Numeric cross-type equality: 42 == 42.0
    if (isNumeric(a) && isNumeric(b))
        return toDouble(a) == toDouble(b);

    // Different variant index → not equal
    if (a.index() != b.index()) return false;

    // Same index → dispatch by type
    if (std::holds_alternative<std::nullptr_t>(a)) return true;
    if (std::holds_alternative<bool>(a))
        return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<GcPtr<GcString>>(a))
        return std::get<GcPtr<GcString>>(a)->value == std::get<GcPtr<GcString>>(b)->value;

    // Containers: deep structural comparison
    if (std::holds_alternative<GcPtr<Array>>(a)) {
        const auto& ae = std::get<GcPtr<Array>>(a)->elements;
        const auto& be = std::get<GcPtr<Array>>(b)->elements;
        if (ae.size() != be.size()) return false;
        for (size_t i = 0; i < ae.size(); i++) {
            if (!ValueEqual{}(ae[i], be[i])) return false;
        }
        return true;
    }
    if (std::holds_alternative<GcPtr<Dict>>(a)) {
        const auto& ap = std::get<GcPtr<Dict>>(a)->pairs;
        const auto& bp = std::get<GcPtr<Dict>>(b)->pairs;
        if (ap.size() != bp.size()) return false;
        for (const auto& [k, v] : ap) {
            auto it = bp.find(k);
            if (it == bp.end()) return false;
            if (!ValueEqual{}(v, it->second)) return false;
        }
        return true;
    }
    if (std::holds_alternative<GcPtr<Set>>(a)) {
        const auto& as = std::get<GcPtr<Set>>(a)->elements;
        const auto& bs = std::get<GcPtr<Set>>(b)->elements;
        if (as.size() != bs.size()) return false;
        for (const auto& e : as) {
            if (bs.find(e) == bs.end()) return false;
        }
        return true;
    }
    if (std::holds_alternative<GcPtr<Map>>(a)) {
        const auto& ap = std::get<GcPtr<Map>>(a)->pairs;
        const auto& bp = std::get<GcPtr<Map>>(b)->pairs;
        if (ap.size() != bp.size()) return false;
        for (const auto& [k, v] : ap) {
            auto it = bp.find(k);
            if (it == bp.end()) return false;
            if (!ValueEqual{}(v, it->second)) return false;
        }
        return true;
    }

    // Reference types: pointer identity
    if (std::holds_alternative<GcPtr<Callable>>(a))
        return std::get<GcPtr<Callable>>(a) == std::get<GcPtr<Callable>>(b);
    if (std::holds_alternative<GcPtr<ObjectInstance>>(a))
        return std::get<GcPtr<ObjectInstance>>(a) == std::get<GcPtr<ObjectInstance>>(b);
    if (std::holds_alternative<GcPtr<FunctionPrototype>>(a))
        return std::get<GcPtr<FunctionPrototype>>(a) == std::get<GcPtr<FunctionPrototype>>(b);
    if (std::holds_alternative<GcPtr<ClassDefinition>>(a))
        return std::get<GcPtr<ClassDefinition>>(a) == std::get<GcPtr<ClassDefinition>>(b);
    if (std::holds_alternative<GcPtr<Iterator>>(a))
        return std::get<GcPtr<Iterator>>(a) == std::get<GcPtr<Iterator>>(b);
    if (std::holds_alternative<GcPtr<Generator>>(a))
        return std::get<GcPtr<Generator>>(a) == std::get<GcPtr<Generator>>(b);

    return false;
}

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
    } else if (auto* p = std::get_if<GcPtr<Set>>(&v)) {
        if (*p) worklist.push_back(p->get());
    } else if (auto* p = std::get_if<GcPtr<Map>>(&v)) {
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
    for (auto& mp : staticMethodProtos) {
        if (mp) wl.push_back(mp.get());
    }
    for (auto& [name, fn] : methods) {
        if (fn) wl.push_back(fn.get());
    }
    for (auto& [name, fn] : staticMethods) {
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

void Set::trace(std::vector<GcObject*>& wl) {
    for (const auto& e : elements) pushGcRefs(e, wl);
}

void Map::trace(std::vector<GcObject*>& wl) {
    for (const auto& [k, v] : pairs) {
        pushGcRefs(k, wl);
        pushGcRefs(v, wl);
    }
}

void Iterator::trace(std::vector<GcObject*>& wl) {
    pushGcRefs(source, wl);
    for (auto& vk : valueKeys) pushGcRefs(vk, wl);
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
        } else if constexpr (std::is_same_v<T, GcPtr<Set>>) {
            out += '{';
            size_t i = 0;
            for (const auto& e : arg->elements) {
                if (i > 0) out += ", ";
                appendValue(out, e, depth + 1);
                i++;
            }
            out += '}';
        } else if constexpr (std::is_same_v<T, GcPtr<Map>>) {
            out += '{';
            size_t i = 0;
            for (const auto& [k, v] : arg->pairs) {
                if (i > 0) out += ", ";
                appendValue(out, k, depth + 1);
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
