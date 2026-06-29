#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
// NaN-boxing Value — 8-byte tagged union using IEEE 754 NaN space.
//
// Bit layout for tagged (non-double) values:
//   [63]     = 1  (sign — negative NaN)
//   [62:52]  = 0x7FF  (NaN exponent)
//   [51]     = 1  (quiet NaN)
//   [50]     = 1  (VORA_TAG_BIT — distinguishes from hardware NaN)
//   [49:46]  = type tag (4 bits, 16 types)
//   [45:0]   = payload (46 bits — pointer or immediate data)
//
// Genuine IEEE 754 doubles (including ±inf, subnormals, and hardware NaN)
// have (bits_ & TAG_MARKER) != TAG_MARKER.
// =========================================================================

// --- Bit constants ---
static constexpr uint64_t SIGN_MASK    = 0x8000000000000000ULL;
static constexpr uint64_t NAN_EXP_MASK = 0x7FF0000000000000ULL;
static constexpr uint64_t QNAN_BIT     = 0x0008000000000000ULL;  // bit 51
static constexpr uint64_t VORA_TAG_BIT = 0x0004000000000000ULL;  // bit 50
static constexpr uint64_t TAG_MASK     = 0x0003C00000000000ULL;  // bits 49:46
static constexpr uint64_t PAYLOAD_MASK = 0x00003FFFFFFFFFFFULL;  // bits 45:0
static constexpr int      TAG_SHIFT    = 46;
static constexpr uint64_t TAG_MARKER   = SIGN_MASK | NAN_EXP_MASK | QNAN_BIT | VORA_TAG_BIT;
// TAG_MARKER = 0xFFFC000000000000

// --- Type tags for NaN-boxed values ---
enum class ValueTag : uint8_t {
    Null             = 0,
    Bool             = 1,
    Int              = 2,
    GcString         = 3,
    Array            = 4,
    Dict             = 5,
    Callable         = 6,
    ObjectInstance   = 7,
    FunctionPrototype = 8,
    ClassDefinition  = 9,
    Iterator         = 10,
    Generator        = 11,
    Set              = 12,
    Map              = 13,
    // 14-15 reserved
};

// Dispatch tag for switch statements (replaces std::visit).
// Values 0-13 match ValueTag exactly. Double = 14 is a pseudo-tag.
enum class DispatchTag : uint8_t {
    Null             = 0,
    Bool             = 1,
    Int              = 2,
    GcString         = 3,
    Array            = 4,
    Dict             = 5,
    Callable         = 6,
    ObjectInstance   = 7,
    FunctionPrototype = 8,
    ClassDefinition  = 9,
    Iterator         = 10,
    Generator        = 11,
    Set              = 12,
    Map              = 13,
    Double           = 14,
};

// int47 range for inline integers
static constexpr int64_t INT47_MAX =  0x00001FFFFFFFFFFFLL;  //  35,184,372,088,831
static constexpr int64_t INT47_MIN = -0x0000200000000000LL;  // -35,184,372,088,832

// =========================================================================
// Value — the universal runtime value type (NaN-boxed, 8 bytes)
// =========================================================================

class Value {
public:
    using Raw = uint64_t;

    // --- Construction ---
    Value() = default;
    Value(std::nullptr_t) : Value() {}
    /*implicit*/ Value(bool b) : bits_(tagBits(ValueTag::Bool, b ? 1ULL : 0ULL)) {}
    /*implicit*/ Value(int64_t v);
    /*implicit*/ Value(double d);

    // GcPtr<T> constructors — one per heap type
    /*implicit*/ Value(GcPtr<struct GcString> p)    : bits_(tagBits(ValueTag::GcString, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Array> p)        : bits_(tagBits(ValueTag::Array, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Dict> p)         : bits_(tagBits(ValueTag::Dict, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<Callable> p)            : bits_(tagBits(ValueTag::Callable, ptrPayload(p.get()))) {}
    // Catch-all for Callable subtypes (VoraFunction, NativeFunction, BoundMethod, ClassConstructor)
    // that would otherwise require two-step implicit conversion (Derived→Callable→Value).
    template<typename T, typename = std::enable_if_t<std::is_base_of_v<Callable, T>>>
    /*implicit*/ Value(GcPtr<T> p) : bits_(tagBits(ValueTag::Callable, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct ObjectInstance> p): bits_(tagBits(ValueTag::ObjectInstance, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<FunctionPrototype> p)   : bits_(tagBits(ValueTag::FunctionPrototype, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<ClassDefinition> p)     : bits_(tagBits(ValueTag::ClassDefinition, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Iterator> p)     : bits_(tagBits(ValueTag::Iterator, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Generator> p)    : bits_(tagBits(ValueTag::Generator, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Set> p)          : bits_(tagBits(ValueTag::Set, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Map> p)          : bits_(tagBits(ValueTag::Map, ptrPayload(p.get()))) {}

    // Value is trivially copyable
    Value(const Value&) = default;
    Value& operator=(const Value&) = default;

    // Raw access (for GC, hashing, serialization)
    Raw raw() const { return bits_; }
    explicit Value(Raw r) : bits_(r) {}

    // --- Type queries ---
    ValueTag tag() const { return static_cast<ValueTag>((bits_ & TAG_MASK) >> TAG_SHIFT); }
    DispatchTag dispatchTag() const {
        return isDouble() ? DispatchTag::Double : static_cast<DispatchTag>(static_cast<uint8_t>(tag()));
    }

    bool isDouble() const { return (bits_ & TAG_MARKER) != TAG_MARKER; }
    bool isNull()   const { return !isDouble() && tag() == ValueTag::Null; }
    bool isBool()   const { return !isDouble() && tag() == ValueTag::Bool; }
    bool isInt()    const { return !isDouble() && tag() == ValueTag::Int; }
    bool isGcString()          const { return !isDouble() && tag() == ValueTag::GcString; }
    bool isArray()             const { return !isDouble() && tag() == ValueTag::Array; }
    bool isDict()              const { return !isDouble() && tag() == ValueTag::Dict; }
    bool isCallable()          const { return !isDouble() && tag() == ValueTag::Callable; }
    bool isObjectInstance()    const { return !isDouble() && tag() == ValueTag::ObjectInstance; }
    bool isFunctionPrototype() const { return !isDouble() && tag() == ValueTag::FunctionPrototype; }
    bool isClassDefinition()   const { return !isDouble() && tag() == ValueTag::ClassDefinition; }
    bool isIterator()          const { return !isDouble() && tag() == ValueTag::Iterator; }
    bool isGenerator()         const { return !isDouble() && tag() == ValueTag::Generator; }
    bool isSet()               const { return !isDouble() && tag() == ValueTag::Set; }
    bool isMap()               const { return !isDouble() && tag() == ValueTag::Map; }
    bool isNumeric()           const { return isInt() || isDouble(); }

    // True for any GcObject-backed type (tags 3–13)
    bool isObject() const {
        if (isDouble()) return false;
        auto t = tag();
        return t >= ValueTag::GcString && t <= ValueTag::Map;
    }

    // --- Value extractors ---
    bool    asBool()   const;
    int64_t asInt()    const;
    double  asDouble() const;  // promotes int→double, returns 0.0 for non-numeric

    // Generic GcObject* extraction (for GC scanning)
    GcObject* asObject() const { return reinterpret_cast<GcObject*>(bits_ & PAYLOAD_MASK); }

    // Typed GcPtr extractors
    GcPtr<GcString>          asGcString()          const { return GcPtr<GcString>(reinterpret_cast<GcString*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Array>             asArray()             const { return GcPtr<Array>(reinterpret_cast<Array*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Dict>              asDict()              const { return GcPtr<Dict>(reinterpret_cast<Dict*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Callable>          asCallable()          const { return GcPtr<Callable>(reinterpret_cast<Callable*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<ObjectInstance>    asObjectInstance()    const { return GcPtr<ObjectInstance>(reinterpret_cast<ObjectInstance*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<FunctionPrototype> asFunctionPrototype() const { return GcPtr<FunctionPrototype>(reinterpret_cast<FunctionPrototype*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<ClassDefinition>   asClassDefinition()   const { return GcPtr<ClassDefinition>(reinterpret_cast<ClassDefinition*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Iterator>          asIterator()          const { return GcPtr<Iterator>(reinterpret_cast<Iterator*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Generator>         asGenerator()         const { return GcPtr<Generator>(reinterpret_cast<Generator*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Set>               asSet()               const { return GcPtr<Set>(reinterpret_cast<Set*>(bits_ & PAYLOAD_MASK)); }
    GcPtr<Map>               asMap()               const { return GcPtr<Map>(reinterpret_cast<Map*>(bits_ & PAYLOAD_MASK)); }

    // --- Comparison (bitwise identity) ---
    bool operator==(const Value& other) const { return bits_ == other.bits_; }
    bool operator!=(const Value& other) const { return bits_ != other.bits_; }

private:
    Raw bits_ = TAG_MARKER;  // default: null (tag 0, payload 0)

    static Raw tagBits(ValueTag tag, uint64_t payload) {
        return TAG_MARKER | (static_cast<uint64_t>(tag) << TAG_SHIFT) | (payload & PAYLOAD_MASK);
    }

    static uint64_t ptrPayload(void* p) {
        return reinterpret_cast<uint64_t>(p) & PAYLOAD_MASK;
    }
};

static_assert(sizeof(Value) == 8, "NaN-boxed Value must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<Value>, "Value must be trivially copyable");
static_assert(std::is_trivially_destructible_v<Value>, "Value must be trivially destructible");

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
// Value non-inline constructors and extractors
// =========================================================================

inline Value::Value(int64_t v) {
    // Clamp to int47 range
    if (v > INT47_MAX) v = INT47_MAX;
    else if (v < INT47_MIN) v = INT47_MIN;
    bits_ = tagBits(ValueTag::Int, static_cast<uint64_t>(v) & PAYLOAD_MASK);
}

inline Value::Value(double d) {
    uint64_t raw;
    std::memcpy(&raw, &d, sizeof(raw));
    // Ensure hardware NaN doubles don't have VORA_TAG_BIT set,
    // so they are never misclassified as our tagged values.
    // FP operations produce positive quiet NaN (0x7FF8...), which
    // already has bit 50 clear. We clear it defensively in case of
    // non-standard NaN payloads.
    if ((raw & NAN_EXP_MASK) == NAN_EXP_MASK && (raw & QNAN_BIT)) {
        raw &= ~VORA_TAG_BIT;
    }
    bits_ = raw;
}

inline bool Value::asBool() const {
    return (bits_ & 1ULL) != 0;
}

inline int64_t Value::asInt() const {
    uint64_t payload = bits_ & PAYLOAD_MASK;
    // Sign-extend from bit 45 to bits 63:46
    return static_cast<int64_t>(payload << 18) >> 18;
}

inline double Value::asDouble() const {
    if (isDouble()) {
        double d;
        std::memcpy(&d, &bits_, sizeof(d));
        return d;
    }
    if (isInt()) {
        return static_cast<double>(asInt());
    }
    return 0.0;
}

// =========================================================================
// Value API (free functions)
// =========================================================================

void printValue(const Value& value);
std::string valueToString(const Value& value);

// Append value's string representation directly to `out` (no intermediate
// allocation).  Used by valueToString, printValue, and addValues (string
// concatenation).  depth = current nesting depth (starts at 0).
void valueToStringAppend(std::string& out, const Value& value, int depth = 0);

inline bool isNumeric(const Value& v) {
    return v.isNumeric();
}

inline double toDouble(const Value& v) {
    return v.asDouble();
}

inline Value promoteToFloat(const Value& v) {
    if (v.isInt()) return Value(static_cast<double>(v.asInt()));
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
