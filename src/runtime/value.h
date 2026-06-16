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

// Append value's string representation directly to `out` (no intermediate
// allocation).  Used by valueToString, printValue, and addValues (string
// concatenation).  depth = current nesting depth (starts at 0).
void valueToStringAppend(std::string& out, const Value& value, int depth = 0);

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
// Upvalue — index-based indirection for closure-captured variables.
//
// When "open" (isClosed=false): stackPtr references the VM's stack vector,
// slotIndex is the absolute index into that vector. Reads and writes go
// directly to the live stack slot — closures sharing the same upvalue see
// each other's mutations immediately.
//
// When "closed" (isClosed=true): the local has gone out of scope. The
// value has been copied to `closed`, and all reads/writes use that copy.
//
// This design avoids storing raw Value* into a std::vector that could
// reallocate on push_back — the vector object itself is stable (a member
// of VM), and slotIndex stays valid regardless of internal buffer moves.
// =========================================================================

struct Upvalue {
    std::vector<Value>* stackPtr = nullptr;  // Pointer to VM's stack vector (stable address)
    size_t slotIndex = 0;                     // Absolute index into the stack
    Value closed = nullptr;                   // Own storage when the upvalue is closed
    bool isClosed = false;                    // Whether this upvalue has been closed

    Value get() const {
        return isClosed ? closed : (*stackPtr)[slotIndex];
    }
    void set(const Value& v) {
        if (isClosed) closed = v;
        else (*stackPtr)[slotIndex] = v;
    }
    void close() {
        if (!isClosed) {
            closed = (*stackPtr)[slotIndex];
            isClosed = true;
        }
    }
};

} // namespace vora
