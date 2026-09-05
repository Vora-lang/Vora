#pragma once

#include "gc_object.h"
#include "gc_ptr.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// =========================================================================
/// @file gc_heap.h
/// @brief Singleton generational mark-sweep garbage collector for Vora.
///
/// GcHeap is the single allocation and collection entry point for every
/// garbage-collected heap object.  It maintains an intrusive singly-linked
/// list of all live GcObject instances and implements a **generational**
/// mark-sweep algorithm:
///
///   1. <b>Minor GC</b> (`collectMinor()`) — Frequent, fast.  Only scans
///      young objects (age_ < PROMOTION_THRESHOLD) and the remembered set
///      (old objects that may reference young objects).  Surviving young
///      objects are promoted (age_ incremented); after PROMOTION_THRESHOLD
///      survivals they become old.
///   2. <b>Major GC</b> (`collectMajor()`) — Infrequent, full-heap.  Scans
///      all objects regardless of age.  All survivors are marked old.
///
/// A **write barrier** (`writeBarrier()`) tracks when an old object gains a
/// reference to a young object, recording the old object in a remembered set
/// that serves as additional roots during minor GC.
///
/// The GC is single-threaded by design (the VM runs on one thread).
///
/// @see GcObject, GcPtr
// =========================================================================

namespace vora {

class Value;  // forward declaration

// =========================================================================
/// @brief Extract a raw GcObject* from a NaN-boxed Value if it holds a heap
///        reference.  Returns nullptr for non-heap Values (int, bool, null, etc.).
///
/// Used by the write barrier to determine whether a stored Value references
/// a young heap object.
///
/// @param v The Value to inspect.
/// @return Raw GcObject* if the Value holds a heap reference, else nullptr.
// =========================================================================
GcObject* extractGcObject(const Value& v);

// =========================================================================
/// @brief Write barrier: records old→young references in the remembered set.
///
/// Called whenever a heap object's field is assigned a Value that references
/// another heap object.  If @p src is an old object and @p dst is a young
/// object, @p src is added to the remembered set so minor GC can scan it.
///
/// @param src The object whose field is being written (may be nullptr).
/// @param dst The Value being stored into the field.
// =========================================================================
void writeBarrier(GcObject* src, const Value& dst);

// =========================================================================
/// @class GcHeap
/// @brief Singleton generational garbage collector.
///
/// Every heap allocation goes through GcHeap::allocate() (or the convenience
/// wrapper GcHeap::alloc()).  Allocated objects are threaded onto an
/// internal singly-linked list.
///
/// <b>NaN-boxing invariant:</b> Pointers must fit within the 46-bit payload.
///
/// <b>Generational design:</b>
///   - Young objects start at age_=0, collected by minor GCs
///   - Minor GC scans only young objects + remembered set → fast
///   - Objects surviving PROMOTION_THRESHOLD (3) minor GCs become old
///   - Major GC scans the entire heap (infrequent)
///   - Write barrier tracks old→young cross-generational references
///
/// @note This class is a Meyers singleton — copy and move are deleted.
// =========================================================================

class GcHeap {
public:
    /// @brief Return the global singleton GcHeap instance (Meyers singleton).
    static GcHeap& instance() {
        static GcHeap heap;
        return heap;
    }

    // =========================================================================
    /// @brief Allocate a GC-managed object of type T on the heap.
    ///
    /// Constructs the object with forwarded arguments, inserts it at the head
    /// of the internal allocation list, and updates byte counters.  Triggers
    /// pending minor/major GC flags when thresholds are exceeded.
    ///
    /// @tparam T    The GcObject subclass to allocate.
    /// @tparam Args Constructor argument types (deduced).
    /// @param args  Arguments forwarded to T's constructor.
    /// @return      Raw pointer to the newly allocated T.
    // =========================================================================
    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
         T* obj = new T(std::forward<Args>(args)...);
         // NaN-boxing safety: pointer must fit in 46-bit shifted payload.
         // Heap pointers are stored as (ptr >> 2) in the tagged payload.
         assert(((reinterpret_cast<uint64_t>(static_cast<void*>(obj)) >> 2) & ~UINT64_C(0x3FFFFFFFFFFF)) == 0
             && "GcObject allocated above 46-bit shifted payload range — NaN-boxing invariant broken");
        obj->gcNext = head_;
        head_ = obj;
        objectCount_++;
        allocatedBytes_ += obj->gcSize();
        youngBytesSinceLastMinorGC_ += obj->gcSize();

        if (youngBytesSinceLastMinorGC_ >= minorGCThreshold_) {
            pendingMinorGC_ = true;
        }
        if (allocatedBytes_ >= nextGCThreshold_) {
            pendingMajorGC_ = true;
        }
        return obj;
    }

    /// @brief Allocate a GC-managed object and wrap it in a GcPtr<T>.
    template <typename T, typename... Args>
    GcPtr<T> alloc(Args&&... args) {
        return GcPtr<T>(allocate<T>(std::forward<Args>(args)...));
    }

    // ── Collection API ───────────────────────────────────────────────────

    /// @brief Run a minor GC: young generation only.
    /// Scans roots + remembered set, marks transitively through young
    /// objects, promotes survivors, and sweeps unmarked young objects.
    /// Old objects are untouched.
    void collectMinor(const std::vector<GcObject*>& roots);

    /// @brief Run a major GC: full heap (all generations).
    /// Scans the full root set, marks the entire object graph, sweeps all
    /// unmarked objects, and resets the remembered set.
    void collectMajor(const std::vector<GcObject*>& roots);

    /// @brief Run a full mark-sweep cycle (backward-compatible alias).
    /// Equivalent to collectMajor().
    void collect(const std::vector<GcObject*>& roots) { collectMajor(roots); }

    /// @brief Check whether a GC cycle is pending.
    bool needsGC() const { return pendingMinorGC_ || pendingMajorGC_; }

    /// @brief Run the appropriate GC cycle(s) if pending.
    /// Runs minor GC first (if pending), then major GC (if still needed).
    void collectIfNeeded(const std::vector<GcObject*>& roots) {
        if (pendingMinorGC_) collectMinor(roots);
        if (pendingMajorGC_) collectMajor(roots);
    }

    // ── Remembered set ───────────────────────────────────────────────────

    /// @brief Add an old object to the remembered set (called by write barrier).
    void addToRememberedSet(GcObject* obj) {
        if (!obj->isRemembered()) {
            obj->setRemembered(true);
            rememberedSet_.push_back(obj);
        }
    }

    /// @brief Clear the remembered set (called after major GC).
    void clearRememberedSet() {
        for (auto* obj : rememberedSet_) {
            obj->setRemembered(false);
        }
        rememberedSet_.clear();
    }

    // ── Statistics ───────────────────────────────────────────────────────

    /// @brief Return the number of live GC-managed objects.
    size_t objectCount() const { return objectCount_; }
    /// @brief Return the total allocated bytes.
    size_t allocatedBytes() const { return allocatedBytes_; }
    /// @brief Return the current major GC threshold in bytes.
    size_t gcThreshold() const { return nextGCThreshold_; }
    /// @brief Return the minor GC threshold.
    size_t minorGCThreshold() const { return minorGCThreshold_; }
    /// @brief Number of minor GCs run.
    size_t minorGCCount() const { return minorGCCount_; }
    /// @brief Number of major GCs run.
    size_t majorGCCount() const { return majorGCCount_; }
    /// @brief Total objects promoted to old generation.
    size_t promotedCount() const { return promotedCount_; }

    /// @brief Reset the heap: delete all tracked objects and clear counters.
    void reset();

    /// @brief Return the head of the internal allocation list (debug).
    GcObject* head() const { return head_; }

private:
    GcHeap() = default;
    ~GcHeap() { reset(); }
    GcHeap(const GcHeap&) = delete;
    GcHeap& operator=(const GcHeap&) = delete;

    /// @brief Mark a single object and transitively trace its children.
    /// During minor GC, skips old objects (they're already known-live).
    void mark(GcObject* obj, std::vector<GcObject*>& worklist, bool isMinor);

    /// @brief Sweep phase for minor GC: only delete unmarked young objects.
    void sweepMinor();

    /// @brief Sweep phase for major GC: delete all unmarked objects.
    void sweepMajor();

    // ── Allocation list ──────────────────────────────────────────────────

    GcObject* head_ = nullptr;
    size_t objectCount_ = 0;
    size_t allocatedBytes_ = 0;

    // ── GC thresholds ────────────────────────────────────────────────────

    size_t nextGCThreshold_ = 4 * 1024 * 1024;       // major GC: 4 MiB
    size_t minorGCThreshold_ = 512 * 1024;            // minor GC: 512 KiB
    size_t youngBytesSinceLastMinorGC_ = 0;
    bool pendingMinorGC_ = false;
    bool pendingMajorGC_ = false;

    // ── Remembered set ───────────────────────────────────────────────────

    std::vector<GcObject*> rememberedSet_;

    // ── Statistics ───────────────────────────────────────────────────────

    size_t minorGCCount_ = 0;
    size_t majorGCCount_ = 0;
    size_t promotedCount_ = 0;
};

} // namespace vora
