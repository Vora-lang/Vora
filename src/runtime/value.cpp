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
    if (v.isDouble()) {
        double d = v.asDouble();
        // Normalize -0.0 to 0.0, and integer-valued doubles to int64
        d = (d == -0.0) ? 0.0 : d;
        if (d == static_cast<double>(static_cast<int64_t>(d))
            && d >= static_cast<double>(INT64_MIN)
            && d < static_cast<double>(INT64_MAX)) {
            return std::hash<int64_t>{}(static_cast<int64_t>(d));
        }
        return std::hash<double>{}(d);
    }

    switch (v.tag()) {
    case ValueTag::Null:
        return 0;
    case ValueTag::Bool:
        return std::hash<bool>{}(v.asBool());
    case ValueTag::Int:
        return std::hash<int64_t>{}(v.asInt());
    case ValueTag::GcString:
        return std::hash<std::string>{}(v.asGcString()->value);
    case ValueTag::Array: {
        size_t h = 0;
        for (const auto& e : v.asArray()->elements) {
            hashCombine(h, ValueHash{}(e));
        }
        return h;
    }
    case ValueTag::Dict: {
        // Dict iteration order is non-deterministic; sort keys for determinism
        std::vector<std::string> keys;
        const auto& pairs = v.asDict()->pairs;
        keys.reserve(pairs.size());
        for (const auto& [k, val] : pairs) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        size_t h = 0;
        for (const auto& k : keys) {
            hashCombine(h, std::hash<std::string>{}(k));
            hashCombine(h, ValueHash{}(pairs.at(k)));
        }
        return h;
    }
    case ValueTag::Set: {
        // Order-independent: XOR individual hashes
        size_t h = 0;
        for (const auto& e : v.asSet()->elements) {
            h ^= ValueHash{}(e);
        }
        return h;
    }
    case ValueTag::Map: {
        // Order-independent XOR of (keyHash ^ valueHash)
        size_t h = 0;
        for (const auto& [k, val] : v.asMap()->pairs) {
            h ^= (ValueHash{}(k) ^ ValueHash{}(val));
        }
        return h;
    }
    default:
        // Identity hash for functions, objects, classes, iterators, generators
        // (all GcObject-backed types with pointer identity)
        return std::hash<const void*>{}(static_cast<const void*>(v.asObject()));
    }
}

// =========================================================================
// ValueEqual — deep structural equality for Value (used by Set/Map)
// =========================================================================

bool ValueEqual::operator()(const Value& a, const Value& b) const {
    // Numeric cross-type equality: 42 == 42.0
    if (isNumeric(a) && isNumeric(b))
        return toDouble(a) == toDouble(b);

    // Different dispatch tag → not equal
    if (a.dispatchTag() != b.dispatchTag()) return false;

    if (a.isDouble()) {
        // Both are doubles: bitwise compare
        return a.raw() == b.raw();
    }

    // Same type tag → dispatch by type
    switch (a.tag()) {
    case ValueTag::Null:
        return true;
    case ValueTag::Bool:
        return a.asBool() == b.asBool();
    case ValueTag::Int:
        return a.asInt() == b.asInt();
    case ValueTag::GcString:
        return a.asGcString()->value == b.asGcString()->value;

    case ValueTag::Array: {
        const auto& ae = a.asArray()->elements;
        const auto& be = b.asArray()->elements;
        if (ae.size() != be.size()) return false;
        for (size_t i = 0; i < ae.size(); i++) {
            if (!ValueEqual{}(ae[i], be[i])) return false;
        }
        return true;
    }
    case ValueTag::Dict: {
        const auto& ap = a.asDict()->pairs;
        const auto& bp = b.asDict()->pairs;
        if (ap.size() != bp.size()) return false;
        for (const auto& [k, v] : ap) {
            auto it = bp.find(k);
            if (it == bp.end()) return false;
            if (!ValueEqual{}(v, it->second)) return false;
        }
        return true;
    }
    case ValueTag::Set: {
        const auto& as = a.asSet()->elements;
        const auto& bs = b.asSet()->elements;
        if (as.size() != bs.size()) return false;
        for (const auto& e : as) {
            if (bs.find(e) == bs.end()) return false;
        }
        return true;
    }
    case ValueTag::Map: {
        const auto& ap = a.asMap()->pairs;
        const auto& bp = b.asMap()->pairs;
        if (ap.size() != bp.size()) return false;
        for (const auto& [k, v] : ap) {
            auto it = bp.find(k);
            if (it == bp.end()) return false;
            if (!ValueEqual{}(v, it->second)) return false;
        }
        return true;
    }

    // Reference types: pointer identity
    case ValueTag::Callable:
        return a.asCallable() == b.asCallable();
    case ValueTag::ObjectInstance:
        return a.asObjectInstance() == b.asObjectInstance();
    case ValueTag::FunctionPrototype:
        return a.asFunctionPrototype() == b.asFunctionPrototype();
    case ValueTag::ClassDefinition:
        return a.asClassDefinition() == b.asClassDefinition();
    case ValueTag::Iterator:
        return a.asIterator() == b.asIterator();
    case ValueTag::Generator:
        return a.asGenerator() == b.asGenerator();
    }

    return false;
}

// =========================================================================
// pushGcRefs — extract all GcObject* references from a Value.
//
// With NaN-boxing, all 11 GcObject-backed types share the same payload
// encoding (pointer in bits 45:0). This collapses what was an 11-branch
// std::get_if chain into a single isObject() + asObject().
// =========================================================================

void pushGcRefs(const Value& v, std::vector<GcObject*>& worklist) {
    if (v.isObject()) {
        GcObject* obj = v.asObject();
        if (obj) worklist.push_back(obj);
    }
}

// =========================================================================
// trace() implementations — unchanged bodies (they call pushGcRefs above)
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

static void appendValue(std::string& out, const Value& value, int depth);

static void appendValue(std::string& out, const Value& value, int depth) {
    if (depth > kMaxPrintDepth) {
        out += "[max depth]";
        return;
    }

    if (value.isDouble()) {
        double d = value.asDouble();
        out += std::to_string(d);
        return;
    }

    switch (value.tag()) {
    case ValueTag::Null:
        out += "null";
        break;
    case ValueTag::Bool:
        out += (value.asBool() ? "true" : "false");
        break;
    case ValueTag::Int:
        out += std::to_string(value.asInt());
        break;
    case ValueTag::GcString:
        out += value.asGcString()->value;
        break;
    case ValueTag::Array: {
        out += '[';
        const auto& elements = value.asArray()->elements;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) out += ", ";
            appendValue(out, elements[i], depth + 1);
        }
        out += ']';
        break;
    }
    case ValueTag::Dict: {
        out += '{';
        size_t i = 0;
        for (const auto& [k, v] : value.asDict()->pairs) {
            if (i > 0) out += ", ";
            out += k;
            out += ": ";
            appendValue(out, v, depth + 1);
            i++;
        }
        out += '}';
        break;
    }
    case ValueTag::Set: {
        out += '{';
        size_t i = 0;
        for (const auto& e : value.asSet()->elements) {
            if (i > 0) out += ", ";
            appendValue(out, e, depth + 1);
            i++;
        }
        out += '}';
        break;
    }
    case ValueTag::Map: {
        out += '{';
        size_t i = 0;
        for (const auto& [k, v] : value.asMap()->pairs) {
            if (i > 0) out += ", ";
            appendValue(out, k, depth + 1);
            out += ": ";
            appendValue(out, v, depth + 1);
            i++;
        }
        out += '}';
        break;
    }
    case ValueTag::Callable: {
        auto callable = value.asCallable();
        if (auto native = dynamic_cast<NativeFunction*>(callable.get())) {
            out += "<native fn ";
            out += native->name();
            out += '>';
        } else if (auto fn = dynamic_cast<VoraFunction*>(callable.get())) {
            out += "<fn ";
            out += fn->name();
            out += '>';
        } else {
            out += "<fn>";
        }
        break;
    }
    case ValueTag::ObjectInstance:
        out += '<';
        out += value.asObjectInstance()->className;
        out += " object>";
        break;
    case ValueTag::Iterator:
        out += "<iterator>";
        break;
    case ValueTag::Generator: {
        out += "<generator ";
        auto gen = value.asGenerator();
        out += (gen->function ? gen->function->name() : "?");
        out += '>';
        break;
    }
    case ValueTag::FunctionPrototype:
        out += "<function prototype ";
        out += value.asFunctionPrototype()->name;
        out += '>';
        break;
    case ValueTag::ClassDefinition:
        out += "<class ";
        out += value.asClassDefinition()->name;
        out += '>';
        break;
    }
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

} // namespace vora
