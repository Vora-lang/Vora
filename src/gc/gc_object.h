#pragma once

#include <cstddef>
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
/// =========================================================================

namespace vora {

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
