#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "../gc/gc_object.h"
#include "../gc/gc_ptr.h"

namespace vora {

// Forward declarations for types defined elsewhere
struct Chunk;        // defined in vm/chunk.h
class Callable;
class VoraFunction;
class NativeFunction;
struct BlockStmt;
struct FunctionPrototype;   // defined in compiler.h (needs Chunk)
struct ClassDefinition;     // defined in compiler.h
struct Set;                 // defined below (needs ValueHash/ValueEqual)
struct Map;                 // defined below (needs ValueHash/ValueEqual)

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
    GcPtr<ClassDefinition>,
    GcPtr<struct Iterator>,
    GcPtr<struct Generator>,
    GcPtr<struct Set>,
    GcPtr<struct Map>
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
    size_t gcSize() const override {
        size_t sz = sizeof(ObjectInstance) + className.capacity();
        for (const auto& [key, val] : properties) {
            sz += key.capacity() + sizeof(Value);
        }
        return sz;
    }
};

// Iterator — created by iter(), advanced by next().
// Holds a reference to the source collection and the current index.
// Dict iterators snapshot keys at creation time for stable, O(1) advancement.
// Set/Map iterators use valueKeys for the same purpose.
struct Iterator : GcObject {
    Value source;     // The collection (Array, GcString, Dict, Set, Map) — retains GC reference
    size_t index = 0; // Current read position (0-based)
    std::vector<std::string> dictKeys; // Key snapshot for Dict iteration
    std::vector<Value> valueKeys;      // Key/element snapshot for Set/Map iteration

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override {
        return sizeof(Iterator)
            + dictKeys.capacity() * sizeof(std::string)
            + valueKeys.capacity() * sizeof(Value);
    }
};

// Generator — created by calling a generator function, driven by next().
// Holds the suspended execution context between yield/resume cycles.
struct Generator : GcObject {
    GcPtr<VoraFunction> function;  // The generator bytecode
    bool done = false;             // true when generator exhausted
    bool firstResume = true;       // true before first next() call

    // Args captured when gen_func(a, b, c) was called
    std::vector<Value> args;

    // Saved generator state (written by OP_YIELD)
    size_t savedIpOffset = 0;       // Offset into function's chunk
    size_t savedFrameBase = 0;
    std::vector<Value> savedStack;  // Locals snapshot

    // Saved caller state (written by next(), restored by OP_YIELD/OP_RETURN)
    const uint8_t* callerIp = nullptr;
    const Chunk* callerChunk = nullptr;
    size_t callerFrameBase = 0;
    size_t callerFramesSize = 0;

    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override { return sizeof(Generator) + savedStack.capacity() * sizeof(Value) + args.capacity() * sizeof(Value); }
};

// =========================================================================
// ValueHash & ValueEqual — hashing and deep equality for Value keys
// (used by Set and Map internally; Set/Map are defined below)
// =========================================================================

struct ValueHash {
    size_t operator()(const Value& v) const;
};

struct ValueEqual {
    bool operator()(const Value& a, const Value& b) const;
};

// =========================================================================
// Set — unordered collection of unique Values with O(1) membership
// =========================================================================

struct Set : GcObject {
    std::unordered_set<Value, ValueHash, ValueEqual> elements;
    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override {
        return sizeof(Set) + elements.bucket_count() * (sizeof(void*) * 2 + sizeof(Value));
    }
};

// =========================================================================
// Map — unordered key-value mapping with arbitrary Value keys
// =========================================================================

struct Map : GcObject {
    std::unordered_map<Value, Value, ValueHash, ValueEqual> pairs;
    void trace(std::vector<GcObject*>& wl) override;
    size_t gcSize() const override {
        return sizeof(Map) + pairs.bucket_count() * (sizeof(void*) * 2 + sizeof(Value) * 2);
    }
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
