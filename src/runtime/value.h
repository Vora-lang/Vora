#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <map>

#include "../gc/gc_object.h"
#include "../gc/gc_ptr.h"

namespace vora {

// Forward declarations for types defined elsewhere
class Callable;
class VoraFunction;
class NativeFunction;
struct BlockStmt;
struct FunctionPrototype;   // defined in compiler.h (needs Chunk)
struct ClassDefinition;     // defined in compiler.h

// =========================================================================
// Value — the universal runtime value type
// =========================================================================

using Value = std::variant<
    std::nullptr_t,
    double,
    int64_t,
    bool,
    GcPtr<struct GcString>,
    GcPtr<struct Array>,
    GcPtr<struct Dict>,
    GcPtr<Callable>,
    GcPtr<struct ObjectInstance>,
    GcPtr<FunctionPrototype>,
    GcPtr<ClassDefinition>
>;

// =========================================================================
// pushGcRefs — extract all GcObject* from a Value onto a worklist.
// DECLARED here, DEFINED in value.cpp (needs complete types).
// =========================================================================
void pushGcRefs(const Value& v, std::vector<GcObject*>& worklist);

// =========================================================================
// Heap object definitions (trace() defined in value.cpp / compiler.cpp)
// =========================================================================

struct Array : GcObject {
    std::vector<Value> elements;
    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(Array) + elements.capacity() * sizeof(Value); }
};

struct Dict : GcObject {
    std::unordered_map<std::string, Value> pairs;
    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(Dict) + pairs.bucket_count() * (sizeof(void*) * 2 + sizeof(Value)); }
};

struct GcString : GcObject {
    std::string value;
    GcString() = default;
    explicit GcString(std::string s) : value(std::move(s)) {}
    void trace(std::vector<GcObject*>& wl) override {}  // no GcObject references
    size_t gcSize() const override { return sizeof(GcString) + value.capacity(); }
};

struct ObjectInstance : GcObject {
    std::string className;
    std::map<std::string, Value> properties;
    GcPtr<ClassDefinition> classDefinition;

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(ObjectInstance); }
};

// =========================================================================
// Value API
// =========================================================================

void printValue(const Value& value);
std::string valueToString(const Value& value);

inline bool isNumeric(const Value& v) {
    return std::holds_alternative<double>(v) || std::holds_alternative<int64_t>(v);
}

inline double toDouble(const Value& v) {
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    return 0.0;
}

inline Value promoteToFloat(const Value& v) {
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    return v;
}

// =========================================================================
// Upvalue — pointer-indirection for closure-captured variables.
// When "open", location points to a live VM stack slot.
// When "closed" (local went out of scope), location points to `closed`.
// =========================================================================

struct Upvalue {
    Value* location = nullptr;  // Points to stack slot (open) or &closed (closed)
    Value closed = nullptr;      // Own storage when local goes out of scope

    Value get() const { return *location; }
    void set(const Value& v) { *location = v; }
    void close() { closed = *location; location = &closed; }
};

} // namespace vora
