#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// =========================================================================
/// @file gc_object.h
/// @brief Base class for all garbage-collected heap objects in the Vora runtime.
///
/// GcObject provides the common interface and intrusive linked-list plumbing
/// that the tracing garbage collector needs to discover, mark, and sweep every
/// live heap object.  Every heap-allocated type (Array, Dict, ObjectInstance,
/// VoraFunction, BoundMethod, ClassConstructor, etc.) inherits from GcObject
/// and implements trace() and gcSize().
///
/// The GC uses the @ref gcNext pointer to thread all live GcObjects into a
/// singly-linked allocation list, avoiding a separate external registry.
///
/// ## Generational GC (v0.28+)
///
/// Each object tracks its @ref age_ (number of minor GCs survived).  Young
/// objects (age_ < PROMOTION_THRESHOLD) are collected by frequent minor GCs;
/// old objects (age_ >= PROMOTION_THRESHOLD) are only collected by major GCs.
/// The @ref remembered_ flag marks old objects that may reference young
/// objects, so the remembered set can be scanned as roots during minor GC.
/// =========================================================================

namespace vora {

/// @brief Number of minor GC cycles a young object must survive before
///        promotion to the old generation.
constexpr uint8_t PROMOTION_THRESHOLD = 3;

// =========================================================================
/// @class GcObject
/// @brief Intrusive base class for every garbage-collected heap object.
///
/// Subclasses must implement:
///   - trace(worklist) — push every GcObject* reachable from `this` onto the
///                       worklist so the mark phase can transitively trace the
///                       full object graph.
///   - gcSize()        — return an approximate byte size (used for heap
///                       accounting and triggering the next GC cycle).
///
/// ## Generational fields
///
/// - @ref age_ tracks the number of minor GC cycles this object has survived.
///   Young objects (age_ < PROMOTION_THRESHOLD) are collected by minor GCs.
///   After each minor GC survival, age_ is incremented.  Objects that reach
///   PROMOTION_THRESHOLD are promoted to the old generation and are only
///   collected by major GCs.
/// - @ref remembered_ is set by the write barrier when an OLD object gains
///   a reference to a YOUNG object.  During minor GC, the remembered set
///   (old objects with remembered_=true) is scanned as additional roots.
/// =========================================================================

class GcObject {
public:
    virtual ~GcObject() = default;

    // =========================================================================
    /// @brief Push every GcObject* reachable from `this` onto `worklist`.
    ///
    /// The GC mark phase calls this on each reachable object to transitively
    /// trace the full object graph.  Subclasses override it to push their
    /// contained GcObject pointers (e.g. array elements, dictionary keys/values,
    /// object instance fields, closure upvalues) onto @p worklist.
    ///
    /// @param worklist  Output vector that receives every directly-reachable
    ///                  GcObject*.  The mark loop pops objects from this list
    ///                  and calls their trace() in turn until the list is empty.
    // =========================================================================
    virtual void trace(std::vector<GcObject*>& worklist) = 0;

    // =========================================================================
    /// @brief Return an approximate byte size for heap-accounting purposes.
    ///
    /// The default implementation returns `sizeof(*this)`.  Subclasses that
    /// manage additional heap memory (e.g. dynamic arrays, hash tables) should
    /// override this to include that allocation so the GC can make informed
    /// decisions about when to trigger the next collection cycle.
    ///
    /// @return Approximate number of heap bytes occupied by this object.
    // =========================================================================
    virtual size_t gcSize() const { return sizeof(*this); }

    // =========================================================================
    /// @brief Return whether the GC has marked this object during the current
    ///        mark phase.
    ///
    /// @return `true` if this object is reachable and therefore live.
    // =========================================================================
    bool isMarked() const { return marked_; }

    // =========================================================================
    /// @brief Set or clear the GC mark on this object.
    ///
    /// The mark phase sets this to `true` when it first discovers the object.
    /// The sweep phase resets it to `false` on surviving objects before the
    /// next collection.
    ///
    /// @param m  New mark state (`true` = reachable, `false` = unreachable).
    // =========================================================================
    void setMarked(bool m) { marked_ = m; }

    // ── Generational GC fields ───────────────────────────────────────────

    /// @brief Number of minor GC cycles this object has survived.
    ///        0 = young (eden), >= PROMOTION_THRESHOLD = old (tenured).
    uint8_t age_ = 0;

    /// @brief True if this object is in the remembered set (old→young ref).
    bool remembered_ = false;

    /// @brief Return the age counter (minor GCs survived).
    uint8_t age() const { return age_; }

    /// @brief Set the age counter.
    void setAge(uint8_t a) { age_ = a; }

    /// @brief Increment age; if threshold reached, object is now old.
    void promote() { if (age_ < PROMOTION_THRESHOLD) age_++; }

    /// @brief True if this object has been promoted to the old generation.
    bool isOld() const { return age_ >= PROMOTION_THRESHOLD; }

    /// @brief True if this object is in the remembered set.
    bool isRemembered() const { return remembered_; }

    /// @brief Set or clear the remembered-set flag.
    void setRemembered(bool r) { remembered_ = r; }

    // ── Allocation list ──────────────────────────────────────────────────

    /// @brief Next pointer for the GC's intrusive singly-linked allocation list.
    ///
    /// The GC threads all live GcObjects into this list so it can sweep without
    /// an external registry.  `nullptr` marks the end of the list.
    GcObject* gcNext = nullptr;

private:
    /// @brief GC mark flag.  Set to `true` during the mark phase if the object
    ///        is reachable from any root.
    bool marked_ = false;
};

} // namespace vora
