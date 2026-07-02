/**
 * @file value.h
 * @brief NaN-boxed runtime value type, heap object definitions, and value utilities.
 *
 * Defines the universal 8-byte Value type using IEEE 754 NaN-boxing for the Vora
 * language runtime.  Also defines all garbage-collected heap objects (Array, Dict,
 * GcString, ObjectInstance, Iterator, Generator, Set, Map), the Upvalue mechanism
 * for closure variable capture, and supporting hash/equality functors.
 *
 * @note Value is trivially copyable and trivially destructible — it fits in a
 *       machine register and requires no constructor/destructor bookkeeping.
 */

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

/// @brief Forward declarations for types defined in other translation units.
/// @{
struct Chunk;        // defined in vm/chunk.h
class Callable;
class VoraFunction;
class NativeFunction;
struct BlockStmt;
struct FunctionPrototype;   // defined in compiler.h (needs Chunk)
struct ClassDefinition;     // defined in compiler.h
struct Set;                 // defined below (needs ValueHash/ValueEqual)
struct Map;                 // defined below (needs ValueHash/ValueEqual)
/// @}

/**
 * @brief NaN-boxing Value — 8-byte tagged union using IEEE 754 NaN space.
 *
 * @details Bit layout for tagged (non-double) values:
 * <pre>
 *   [63]     = 1  (sign — negative NaN)
 *   [62:52]  = 0x7FF  (NaN exponent)
 *   [51]     = 1  (quiet NaN)
 *   [50]     = 1  (VORA_TAG_BIT — distinguishes from hardware NaN)
 *   [49:46]  = type tag (4 bits, 16 types)
 *   [45:0]   = payload (46 bits — pointer or immediate data)
 * </pre>
 *
 * Genuine IEEE 754 doubles (including inf, subnormals, and hardware NaN)
 * have `(bits_ & TAG_MARKER) != TAG_MARKER`.
 *
 * @note The VORA_TAG_BIT (bit 50) is the critical discriminator: all tagged
 *       values set it; all genuine doubles clear it.  On construction of a
 *       double Value, bit 50 is defensively cleared in case the input NaN
 *       payload happens to have it set.
 *
 * @note 46-bit pointers are sufficient for all current 64-bit architectures
 *       because virtual address spaces are 48-bit (x86-64) or 52-bit (ARM64),
 *       and the low 2 bits are always zero due to word alignment.
 */
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

/// @brief 4-bit type tag stored in bits 49:46 of a NaN-boxed Value.
///
/// Each heap-allocated or immediate type gets a unique tag. Tags 0–13 are
/// used; 14 and 15 are reserved. The tag is embedded in the IEEE 754 NaN
/// payload alongside the VORA_TAG_BIT discriminator.
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

/// @brief Flat dispatch tag for switch-based type branching, replacing std::visit.
///
/// Values 0–13 mirror ValueTag exactly. The extra entry Double = 14 serves
/// as a pseudo-tag so that a single switch can handle both tagged and
/// untagged (IEEE 754 double) Values without a separate isDouble() branch.
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

/// @brief Range limits for inline 47-bit integers stored in the NaN payload.
///
/// Integers outside [-35,184,372,088,832 .. 35,184,372,088,831] are clamped
/// on construction. This range is enforced by the 46-bit payload width with
/// sign-extension from bit 45.
// int47 range for inline integers
static constexpr int64_t INT47_MAX =  0x00001FFFFFFFFFFFLL;  //  35,184,372,088,831
static constexpr int64_t INT47_MIN = -0x0000200000000000LL;  // -35,184,372,088,832

// =========================================================================
// Value — the universal runtime value type (NaN-boxed, 8 bytes)
// =========================================================================

class Value {
public:
    /// @brief Underlying storage type (raw 64-bit NaN-boxed word).
    using Raw = uint64_t;

    // --- Construction ---

    /// @brief Default constructor — initializes as Null (tag 0, payload 0).
    Value() = default;
    /// @brief Construct a null Value (same as default).
    Value(std::nullptr_t) : Value() {}
    /// @brief Construct a Bool Value (true = payload bit 0 set, false = clear).
    /*implicit*/ Value(bool b) : bits_(tagBits(ValueTag::Bool, b ? 1ULL : 0ULL)) {}
    /// @brief Construct an Int Value with int47-range clamping.
    /// @param v Integer value; clamped to [-35,184,372,088,832, 35,184,372,088,831].
    /*implicit*/ Value(int64_t v);
    /// @brief Construct a Double Value.  IEEE 754 bit-pattern is stored as-is
    ///        after defensively clearing VORA_TAG_BIT on NaN inputs.
    /// @param d Double-precision floating-point value.
    /*implicit*/ Value(double d);

    /// @name GcPtr-to-Value constructors
    /// Each constructs a tagged Value from a GcPtr to a heap object.
    /// @{
    // GcPtr<T> constructors — one per heap type
    /*implicit*/ Value(GcPtr<struct GcString> p)    : bits_(tagBits(ValueTag::GcString, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Array> p)        : bits_(tagBits(ValueTag::Array, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<struct Dict> p)         : bits_(tagBits(ValueTag::Dict, ptrPayload(p.get()))) {}
    /*implicit*/ Value(GcPtr<Callable> p)            : bits_(tagBits(ValueTag::Callable, ptrPayload(p.get()))) {}
    /// @brief Catch-all converting constructor for Callable subtypes.
    ///
    /// Handles VoraFunction, NativeFunction, BoundMethod, and ClassConstructor
    /// without requiring a two-step implicit conversion (Derived → Callable → Value).
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
    /// @}

    // Value is trivially copyable
    Value(const Value&) = default;
    Value& operator=(const Value&) = default;

    // --- Raw access ---

    /// @brief Return the raw 64-bit NaN-boxed word for GC, hashing, or serialization.
    Raw raw() const { return bits_; }
    /// @brief Construct a Value directly from a raw 64-bit word.
    /// @param r Raw NaN-boxed word.  Caller must ensure the bits are valid.
    ///          Used primarily for deserialization and hash-table re-insertion.
    explicit Value(Raw r) : bits_(r) {}
    // Raw access (for GC, hashing, serialization)

    // --- Type queries ---

    /// @brief Extract the 4-bit ValueTag from bits 49:46.
    /// @return The type tag.  Meaningless if isDouble() is true.
    ValueTag tag() const { return static_cast<ValueTag>((bits_ & TAG_MASK) >> TAG_SHIFT); }
    /// @brief Return a DispatchTag suitable for flat switch statements.
    ///
    /// For tagged values this mirrors tag(); for genuine doubles it returns
    /// DispatchTag::Double.  This allows a single switch to handle all cases:
    /// <pre>
    ///   switch (v.dispatchTag()) {
    ///     case DispatchTag::Int:    ... break;
    ///     case DispatchTag::Double: ... break;
    ///   }
    /// </pre>
    DispatchTag dispatchTag() const {
        return isDouble() ? DispatchTag::Double : static_cast<DispatchTag>(static_cast<uint8_t>(tag()));
    }

    /// @brief True when the Value is a genuine IEEE 754 double (tag marker absent).
    bool isDouble() const { return (bits_ & TAG_MARKER) != TAG_MARKER; }
    /// @brief True when the Value is Null (tag 0, payload 0).
    bool isNull()   const { return !isDouble() && tag() == ValueTag::Null; }
    /// @brief True when the Value is Bool (tag 1).
    bool isBool()   const { return !isDouble() && tag() == ValueTag::Bool; }
    /// @brief True when the Value is an inline Int (tag 2).
    bool isInt()    const { return !isDouble() && tag() == ValueTag::Int; }
    /// @brief True when the Value is a GcString reference (tag 3).
    bool isGcString()          const { return !isDouble() && tag() == ValueTag::GcString; }
    /// @brief True when the Value is an Array reference (tag 4).
    bool isArray()             const { return !isDouble() && tag() == ValueTag::Array; }
    /// @brief True when the Value is a Dict reference (tag 5).
    bool isDict()              const { return !isDouble() && tag() == ValueTag::Dict; }
    /// @brief True when the Value is a Callable reference (tag 6).
    bool isCallable()          const { return !isDouble() && tag() == ValueTag::Callable; }
    /// @brief True when the Value is an ObjectInstance reference (tag 7).
    bool isObjectInstance()    const { return !isDouble() && tag() == ValueTag::ObjectInstance; }
    /// @brief True when the Value is a FunctionPrototype reference (tag 8).
    bool isFunctionPrototype() const { return !isDouble() && tag() == ValueTag::FunctionPrototype; }
    /// @brief True when the Value is a ClassDefinition reference (tag 9).
    bool isClassDefinition()   const { return !isDouble() && tag() == ValueTag::ClassDefinition; }
    /// @brief True when the Value is an Iterator reference (tag 10).
    bool isIterator()          const { return !isDouble() && tag() == ValueTag::Iterator; }
    /// @brief True when the Value is a Generator reference (tag 11).
    bool isGenerator()         const { return !isDouble() && tag() == ValueTag::Generator; }
    /// @brief True when the Value is a Set reference (tag 12).
    bool isSet()               const { return !isDouble() && tag() == ValueTag::Set; }
    /// @brief True when the Value is a Map reference (tag 13).
    bool isMap()               const { return !isDouble() && tag() == ValueTag::Map; }
    /// @brief True for any numeric type (Int or Double).
    bool isNumeric()           const { return isInt() || isDouble(); }

    /// @brief True for any GcObject-backed type (tags 3–13).
    ///
    /// Used by the GC to identify Values that carry a valid heap pointer
    /// in their payload.
    // True for any GcObject-backed type (tags 3–13)
    bool isObject() const {
        if (isDouble()) return false;
        auto t = tag();
        return t >= ValueTag::GcString && t <= ValueTag::Map;
    }

    // --- Value extractors ---

    /// @brief Extract Bool value from payload bit 0.
    /// @return true if bit 0 is set, false otherwise.
    bool    asBool()   const;
    /// @brief Extract Int value by sign-extending the 46-bit payload.
    /// @return The 64-bit signed integer stored in the payload.
    int64_t asInt()    const;
    /// @brief Extract numeric value as double.  Promotes Int internally.
    /// @return The double value, or the Int promoted to double.  Returns 0.0
    ///         for non-numeric types.
    double  asDouble() const;  // promotes int→double, returns 0.0 for non-numeric

    /// @brief Extract payload as a raw GcObject pointer for GC scanning.
    /// @return Pointer to the GcObject header (nullptr for Null or immediate types).
    // Generic GcObject* extraction (for GC scanning)
    GcObject* asObject() const { return reinterpret_cast<GcObject*>(bits_ & PAYLOAD_MASK); }

    /// @name Typed GcPtr extractors
    /// Each returns a GcPtr wrapping the heap pointer stored in the payload.
    /// @{
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
    /// @}

    // --- Comparison (bitwise identity) ---

    /// @brief Bitwise equality — two Values are equal iff their raw 64-bit words match.
    ///
    /// @note This is pure identity comparison, NOT structural/semantic equality.
    ///       Two separately-allocated strings with the same content will compare
    ///       unequal.  Use ValueEqual (below) for deep structural comparison.
    bool operator==(const Value& other) const { return bits_ == other.bits_; }
    /// @brief Bitwise inequality — negation of operator==.
    bool operator!=(const Value& other) const { return bits_ != other.bits_; }

private:
    /// @brief Raw 64-bit NaN-boxed word.  Default: TAG_MARKER (Null, tag 0, payload 0).
    Raw bits_ = TAG_MARKER;  // default: null (tag 0, payload 0)

    /// @brief Pack a type tag and 46-bit payload into a NaN-boxed word.
    /// @param tag     Type tag to store in bits 49:46.
    /// @param payload 46-bit data (pointer or immediate).  Upper bits are masked off.
    /// @return The complete 64-bit NaN-boxed representation.
    static Raw tagBits(ValueTag tag, uint64_t payload) {
        return TAG_MARKER | (static_cast<uint64_t>(tag) << TAG_SHIFT) | (payload & PAYLOAD_MASK);
    }

    /// @brief Extract the 46 low bits of a pointer for NaN-boxing.
    /// @param p Heap object pointer.
    /// @return The pointer value masked to 46 bits (PAYLOAD_MASK).
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

/**
 * @brief Walk a Value and push every contained GcObject pointer onto a GC worklist.
 *
 * For tagged object types (GcString, Array, Dict, Callable, ObjectInstance,
 * FunctionPrototype, ClassDefinition, Iterator, Generator, Set, Map), the
 * direct heap pointer is pushed first, then the object's own trace() method
 * is called to push any transitively referenced objects.
 *
 * @param v        Value to scan.
 * @param worklist Output worklist — each reachable GcObject* is appended.
 *
 * @note Declared here but defined in value.cpp because the implementation
 *       needs the complete definitions of Array, Dict, Iterator, Generator,
 *       Set, Map, ObjectInstance, and Callable (to call their trace() methods).
 */
void pushGcRefs(const Value& v, std::vector<GcObject*>& worklist);

// =========================================================================
// Heap object definitions (trace() defined in value.cpp / compiler.cpp)
// =========================================================================

/// @brief Dynamically-sized ordered list of Values.
///
/// Array is the backing store for Vora's array literal syntax (`[1, 2, 3]`).
/// Elements are stored contiguously in a std::vector<Value> for O(1)
/// indexed access and amortized O(1) append.
struct Array : GcObject {
    /// @brief Element storage.  Each element is a NaN-boxed Value.
    std::vector<Value> elements;

    /// @brief Push all GcObject pointers reachable from this array onto the worklist.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Array) + allocated element capacity.
    size_t gcSize() const override { return sizeof(Array) + elements.capacity() * sizeof(Value); }
};

/// @brief String-keyed unordered dictionary of Values.
///
/// Dict is the backing store for Vora's object literal syntax (`{a: 1, b: 2}`).
/// Keys are std::string; values are NaN-boxed.  Lookup, insertion, and deletion
/// are average O(1) via std::unordered_map.
struct Dict : GcObject {
    /// @brief Key-value storage (string → Value).
    std::unordered_map<std::string, Value> pairs;

    /// @brief Push all GcObject pointers reachable from this dict onto the worklist.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Dict) + bucket overhead + per-element storage.
    size_t gcSize() const override { return sizeof(Dict) + pairs.bucket_count() * (sizeof(void*) * 2 + sizeof(Value)); }
};

/// @brief Heap-allocated string backed by std::string.
///
/// GcString is a GcObject wrapper around std::string.  It contains no inner
/// GcObject references, so its trace() is a no-op.  The GC manages GcString
/// lifetime; the string data itself lives on the C++ heap.
struct GcString : GcObject {
    /// @brief The actual string data.
    std::string value;

    GcString() = default;
    /// @brief Construct from a std::string (move semantics).
    /// @param s String to take ownership of.
    explicit GcString(std::string s) : value(std::move(s)) {}

    /// @brief No-op — GcString contains no GcObject references.
    void trace(std::vector<GcObject*>& wl) override {}  // no GcObject references

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(GcString) + allocated string capacity.
    size_t gcSize() const override { return sizeof(GcString) + value.capacity(); }
};

/// @brief Runtime instance of a Vora class.
///
/// Created by `OP_CLASS` or ClassConstructor.  Holds a reference to its
/// ClassDefinition for method dispatch and MRO traversal, plus an ordered
/// property map (std::map for deterministic iteration order).
struct ObjectInstance : GcObject {
    /// @brief Name of the class (e.g., "Dog" for `class Dog {}`).
    std::string className;

    /// @brief Instance properties (key → Value), ordered alphabetically.
    std::map<std::string, Value> properties;

    /// @brief Reference to the ClassDefinition for method dispatch and MRO.
    GcPtr<ClassDefinition> classDefinition;

    /// @brief Trace all GC-reachable objects: the class definition and property values.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(ObjectInstance) + class name capacity + all property key/value storage.
    size_t gcSize() const override {
        size_t sz = sizeof(ObjectInstance) + className.capacity();
        for (const auto& [key, val] : properties) {
            sz += key.capacity() + sizeof(Value);
        }
        return sz;
    }
};

/// @brief Iteration state for Vora's `for-in` loop.
///
/// Created by the builtin `iter()` function and advanced by `next()`.
/// Holds a GC reference to the source collection and the current read
/// position.  For Dict, keys are snapshotted at creation time into
/// dictKeys for stable, O(1) advancement even if the dict is mutated.
/// Set and Map iterators use valueKeys for the same purpose.
// Iterator — created by iter(), advanced by next().
// Holds a reference to the source collection and the current index.
// Dict iterators snapshot keys at creation time for stable, O(1) advancement.
// Set/Map iterators use valueKeys for the same purpose.
struct Iterator : GcObject {
    /// @brief The collection being iterated (Array, GcString, Dict, Set, or Map).
    ///        Retains a GC reference to prevent collection during iteration.
    Value source;     // The collection (Array, GcString, Dict, Set, Map) — retains GC reference

    /// @brief Current read position (0-based index into the key/element snapshot).
    size_t index = 0; // Current read position (0-based)

    /// @brief Snapshot of string keys for Dict iteration.
    ///        Captured at iter() time to ensure stable iteration order.
    std::vector<std::string> dictKeys; // Key snapshot for Dict iteration

    /// @brief Snapshot of keys/elements for Set/Map iteration.
    ///        Stored as Values to handle arbitrary key types in Set/Map.
    std::vector<Value> valueKeys;      // Key/element snapshot for Set/Map iteration

    /// @brief Trace the source collection and all keys/elements in the snapshots.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Iterator) + key snapshot vector capacities.
    size_t gcSize() const override {
        return sizeof(Iterator)
            + dictKeys.capacity() * sizeof(std::string)
            + valueKeys.capacity() * sizeof(Value);
    }
};

/// @brief Suspended generator execution context.
///
/// Created when a generator function is called (e.g., `gen_func(a, b)`).
/// Driven by the builtin `next()` function, which resumes the generator
/// from its last yield point.  On each OP_YIELD, the generator's local
/// stack and instruction pointer are saved here.  On the next call to
/// next(), the suspended state is restored and execution continues.
// Generator — created by calling a generator function, driven by next().
// Holds the suspended execution context between yield/resume cycles.
struct Generator : GcObject {
    /// @brief The generator's bytecode function.
    GcPtr<VoraFunction> function;  // The generator bytecode

    /// @brief True when the generator has been exhausted (OP_RETURN reached).
    bool done = false;             // true when generator exhausted

    /// @brief True before the first next() call; used to evaluate args on first resume.
    bool firstResume = true;       // true before first next() call

    /// @brief Arguments captured at generator creation time (gen_func(a, b, c)).
    // Args captured when gen_func(a, b, c) was called
    std::vector<Value> args;

    // --- Saved generator state (written by OP_YIELD) ---

    /// @brief Instruction pointer offset into the function's bytecode chunk.
    size_t savedIpOffset = 0;       // Offset into function's chunk
    /// @brief Frame base pointer within the VM's value stack at yield time.
    size_t savedFrameBase = 0;
    /// @brief Snapshot of local variables on the stack at yield time.
    std::vector<Value> savedStack;  // Locals snapshot

    // --- Saved caller state (written by next(), restored by OP_YIELD/OP_RETURN) ---

    /// @brief Caller's instruction pointer, restored when the generator finishes.
    const uint8_t* callerIp = nullptr;
    /// @brief Caller's bytecode chunk, restored when the generator finishes.
    const Chunk* callerChunk = nullptr;
    /// @brief Caller's frame base pointer, restored when the generator finishes.
    size_t callerFrameBase = 0;
    /// @brief Number of frames on the VM call stack when the generator was entered.
    size_t callerFramesSize = 0;

    /// @brief Trace the function reference, saved locals, and captured args.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Generator) + saved stack + args vector capacities.
    size_t gcSize() const override { return sizeof(Generator) + savedStack.capacity() * sizeof(Value) + args.capacity() * sizeof(Value); }
};

// =========================================================================
// ValueHash & ValueEqual — hashing and deep equality for Value keys
// (used by Set and Map internally; Set/Map are defined below)
// =========================================================================

/// @brief Hash functor for Value keys used by std::unordered_set<Value> and std::unordered_map<Value, ...>.
///
/// Computes a structural hash: doubles use their raw bit pattern, ints use
/// their payload, bools/nulls use their tag, and heap objects use the pointer
/// address (identity hash).  Strings are NOT hashed by content — two distinct
/// GcString objects with the same text will hash to different buckets.
struct ValueHash {
    /// @brief Compute hash for a Value.
    /// @param v The Value to hash.
    /// @return A size_t hash suitable for unordered containers.
    size_t operator()(const Value& v) const;
};

/// @brief Deep equality functor for Value keys.
///
/// Compares Values structurally rather than by bitwise identity.  Two ints
/// or bools with the same logical value are equal.  Two doubles with the
/// same bit pattern are equal.  Heap objects (strings, arrays, etc.) compare
/// by pointer identity (same GcObject address).
struct ValueEqual {
    /// @brief Compare two Values for structural equality.
    /// @param a First Value.
    /// @param b Second Value.
    /// @return true if the Values represent the same logical entity.
    bool operator()(const Value& a, const Value& b) const;
};

// =========================================================================
// Set — unordered collection of unique Values with O(1) membership
// =========================================================================

/// @brief Unordered collection of unique Values with O(1) membership testing.
///
/// Backed by std::unordered_set with ValueHash/ValueEqual for structural
/// hashing and equality.  Created via the `set()` builtin or `{1, 2, 3}`
/// set literal syntax.  Duplicate insertions are silently ignored.
struct Set : GcObject {
    /// @brief Element storage — each entry is a unique Value.
    std::unordered_set<Value, ValueHash, ValueEqual> elements;

    /// @brief Trace all GC-reachable Values in the set.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Set) + hash-table bucket overhead + per-element Value storage.
    size_t gcSize() const override {
        return sizeof(Set) + elements.bucket_count() * (sizeof(void*) * 2 + sizeof(Value));
    }
};

// =========================================================================
// Map — unordered key-value mapping with arbitrary Value keys
// =========================================================================

/// @brief Unordered key-value mapping with arbitrary Value keys.
///
/// Unlike Dict (which is string-keyed), Map allows any Value type as a key
/// — ints, doubles, bools, and even heap objects.  Backed by std::unordered_map
/// with ValueHash/ValueEqual.  Created via the `map()` builtin.
///
/// @note Because ValueHash uses pointer identity for heap objects, using a
///       GcString as a Map key hashes by pointer address, not by string content.
///       Two distinct GcString objects containing "hello" will be different keys.
struct Map : GcObject {
    /// @brief Key-value storage (Value → Value).
    std::unordered_map<Value, Value, ValueHash, ValueEqual> pairs;

    /// @brief Trace all GC-reachable Values in both keys and values.
    /// @param wl GC worklist to append to.
    void trace(std::vector<GcObject*>& wl) override;

    /// @brief Conservative size estimate for GC heuristics.
    /// @return sizeof(Map) + hash-table bucket overhead + per-entry key+value storage.
    size_t gcSize() const override {
        return sizeof(Map) + pairs.bucket_count() * (sizeof(void*) * 2 + sizeof(Value) * 2);
    }
};

// =========================================================================
// Value non-inline constructors and extractors
// =========================================================================

/**
 * @brief Construct an inline Int Value with int47-range clamping.
 *
 * Values outside [-35,184,372,088,832, 35,184,372,088,831] are silently
 * clamped to the nearest boundary.  The clamped value is masked into
 * 46 bits and tagged with ValueTag::Int.
 *
 * @param v The integer value to box.
 */
inline Value::Value(int64_t v) {
    // Clamp to int47 range
    if (v > INT47_MAX) v = INT47_MAX;
    else if (v < INT47_MIN) v = INT47_MIN;
    bits_ = tagBits(ValueTag::Int, static_cast<uint64_t>(v) & PAYLOAD_MASK);
}

/**
 * @brief Construct a Double Value from an IEEE 754 double.
 *
 * The raw bit pattern is stored as-is, except that NaN inputs have the
 * VORA_TAG_BIT (bit 50) defensively cleared.  This prevents the tag
 * marker check from misclassifying a hardware NaN as a tagged Value.
 *
 * @note Normal FP operations produce positive quiet NaN (0x7FF8...)
 *       which already has bit 50 clear.  The clearing is defensive against
 *       non-standard NaN payloads.
 *
 * @param d The double-precision value to box.
 */
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

/// @brief Extract bool value from the least-significant bit of the payload.
/// @return true if bit 0 is set, false otherwise.
inline bool Value::asBool() const {
    return (bits_ & 1ULL) != 0;
}

/**
 * @brief Extract int64_t by sign-extending the 46-bit payload.
 *
 * The payload occupies bits [45:0].  Sign-extension copies bit 45 into
 * all bits [63:46], reconstructing the full signed 64-bit integer.
 *
 * @return The sign-extended 64-bit integer value.
 */
inline int64_t Value::asInt() const {
    uint64_t payload = bits_ & PAYLOAD_MASK;
    // Sign-extend from bit 45 to bits 63:46
    return static_cast<int64_t>(payload << 18) >> 18;
}

/**
 * @brief Extract the Value as a double, promoting Int if necessary.
 *
 * @return For Double values, the raw bit pattern reinterpreted as double.
 *         For Int values, the integer promoted to double.
 *         For all other types, 0.0.
 */
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

/// @brief Print a human-readable representation of a Value to stdout.
/// @param value The Value to print.
void printValue(const Value& value);

/// @brief Convert a Value to its string representation.
/// @param value The Value to convert.
/// @return A std::string containing the human-readable representation.
std::string valueToString(const Value& value);

/**
 * @brief Append a Value's string representation to an existing string buffer.
 *
 * This is the core formatting routine used by valueToString(), printValue(),
 * and the VM's addValues (string concatenation) operator.  It appends
 * directly to `out` without intermediate allocations.
 *
 * @param out   Output buffer to append to.
 * @param value Value to format.
 * @param depth Current nesting depth for recursive structures (starts at 0).
 *              Used to detect and prevent infinite recursion on circular
 *              references.
 */
// Append value's string representation directly to `out` (no intermediate
// allocation).  Used by valueToString, printValue, and addValues (string
// concatenation).  depth = current nesting depth (starts at 0).
void valueToStringAppend(std::string& out, const Value& value, int depth = 0);

/// @brief Check if a Value is numeric (Int or Double).
/// @param v The Value to test.
/// @return true if v.isInt() or v.isDouble().
inline bool isNumeric(const Value& v) {
    return v.isNumeric();
}

/// @brief Extract a double from a Value, promoting Int if necessary.
/// @param v The Value to convert.
/// @return v.asDouble() — the double value, int promoted, or 0.0 for non-numeric.
inline double toDouble(const Value& v) {
    return v.asDouble();
}

/// @brief Ensure a Value is a double by promoting Int to double.
///
/// If the Value is an Int, the integer is promoted to double and boxed in a
/// new Double Value.  Otherwise the Value is returned unchanged.
///
/// @param v The Value to promote.
/// @return A Double Value (or the original if already Double).
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

/// @brief Index-based indirection for closure-captured local variables.
///
/// Upvalues allow closures to share live access to a local variable while
/// it is still on the stack ("open" state), and transparently switch to a
/// heap copy ("closed" state) when the enclosing scope exits.  This avoids
/// dangling pointer bugs without requiring every local to be heap-allocated.
///
/// @note The design uses a pointer to the VM's stack *vector* (not a raw
///       Value*) to avoid invalidation when the vector reallocates on push_back.
///       The vector object itself has a stable address (it is a VM member),
///       and slotIndex resolves to the current buffer via operator[].
struct Upvalue {
    /// @brief Pointer to the VM's stack vector (stable address across reallocations).
    std::vector<Value>* stackPtr = nullptr;  // Pointer to VM's stack vector (stable address)

    /// @brief Absolute index into the stack vector for the captured variable.
    size_t slotIndex = 0;                     // Absolute index into the stack

    /// @brief Heap storage for the captured value once closed.
    Value closed = nullptr;                   // Own storage when the upvalue is closed

    /// @brief Whether the captured variable has gone out of scope.
    bool isClosed = false;                    // Whether this upvalue has been closed

    /// @brief Read the captured value.
    ///
    /// If the upvalue is open, reads directly from the live stack slot.
    /// If closed, reads from the heap copy.
    ///
    /// @return The current value of the captured variable.
    Value get() const {
        return isClosed ? closed : (*stackPtr)[slotIndex];
    }

    /// @brief Write to the captured value.
    ///
    /// If the upvalue is open, writes directly to the live stack slot
    /// (visible to all closures sharing this upvalue).
    /// If closed, writes to the heap copy.
    ///
    /// @param v The new value to store.
    void set(const Value& v) {
        if (isClosed) closed = v;
        else (*stackPtr)[slotIndex] = v;
    }

    /// @brief Close the upvalue by copying the live stack value into heap storage.
    ///
    /// Called when the enclosing scope exits and the local variable goes out
    /// of scope.  After closing, all reads and writes go through the heap copy.
    /// Idempotent — calling close() on an already-closed upvalue is a no-op.
    void close() {
        if (!isClosed) {
            closed = (*stackPtr)[slotIndex];
            isClosed = true;
        }
    }
};

} // namespace vora
