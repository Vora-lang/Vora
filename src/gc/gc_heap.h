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
/// @brief Singleton mark-sweep garbage collector for the Vora runtime.
///
/// GcHeap is the single allocation and collection entry point for every
/// garbage-collected heap object.  It maintains an intrusive singly-linked
/// list of all live GcObject instances, triggers GC cycles when allocation
/// pressure exceeds a configurable byte threshold, and implements a classic
/// mark-and-sweep algorithm:
///
///   1. <b>Mark</b>  — Trace the root set transitively via GcObject::trace(),
///                     using an explicit worklist (not recursion) to avoid
///                     stack overflow on deep object graphs.
///   2. <b>Sweep</b> — Walk the allocation list; unmarked objects are
///                     deleted and removed from the list, marked objects
///                     have their mark bit cleared for the next cycle.
///
/// The GC is single-threaded by design (the VM runs on one thread).  If
/// multi-threading is added in the future, this singleton will need
/// synchronization (mutex around allocations and GC cycles) or a per-thread
/// heap model.
///
/// @see GcObject, GcPtr
// =========================================================================

namespace vora {

// =========================================================================
/// @class GcHeap
/// @brief Singleton mark-sweep garbage collector.
///
/// Every heap allocation goes through GcHeap::allocate() (or the convenience
/// wrapper GcHeap::alloc()).  Allocated objects are threaded onto an
/// internal singly-linked list.  When the total allocated byte count exceeds
/// GcHeap::nextGCThreshold_, a GC cycle is marked as pending and will run on
/// the next call to collectIfNeeded() or collect().
///
/// <b>NaN-boxing invariant:</b> The VM uses NaN-boxing to represent Value
/// as a 64-bit word.  Pointers must fit within a 46-bit payload (bits 0–45)
/// so they do not collide with the IEEE 754 NaN tag in bits 47–63.  The
/// allocate() method asserts this constraint on every allocation.
///
/// <b>Usage:</b>
/// @code
///   auto* raw = GcHeap::instance().allocate<MyType>(args...);
///   auto ptr  = GcHeap::instance().alloc<MyType>(args...);   // returns GcPtr<MyType>
///   GcHeap::instance().collectIfNeeded(roots);
/// @endcode
///
/// @note This class is a Meyers singleton — copy and move are deleted.
/// =========================================================================

class GcHeap {
public:
    /// @brief Return the global singleton GcHeap instance (Meyers singleton).
    ///
    /// The instance is created on first call and destroyed at program exit
    /// (static storage duration).  Thread-safe in C++11 and later due to
    /// guaranteed static-local initialization.
    ///
    /// @return Reference to the single GcHeap instance.
    static GcHeap& instance() {
        static GcHeap heap;
        return heap;
    }

    // =========================================================================
    /// @brief Allocate a GC-managed object of type T on the heap.
    ///
    /// Constructs the object with the forwarded arguments, inserts it at the
    /// head of the internal allocation list, and updates the byte counter.
    /// If the total allocated bytes reach or exceed nextGCThreshold_, the
    /// pending GC flag is set so that the next call to collectIfNeeded()
    /// triggers a collection cycle.
    ///
    /// <b>NaN-boxing invariant:</b> The returned pointer must fit within a
    /// 46-bit payload (bits 0–45).  This is true for all current 64-bit
    /// platforms: x86-64 uses 47-bit virtual addresses, ARM64 uses 48-bit.
    /// If this assert ever fires, the pointer must be boxed through a
    /// separate indirection table.
    ///
    /// @tparam T    The GcObject subclass to allocate.
    /// @tparam Args Constructor argument types (deduced).
    /// @param args  Arguments forwarded to T's constructor.
    /// @return      Raw pointer to the newly allocated T.  Callers should
    ///              typically wrap this in a GcPtr<T> or use alloc<T>().
    ///
    /// @see alloc(), GcPtr
    // =========================================================================
    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        T* obj = new T(std::forward<Args>(args)...);
        // NaN-boxing safety: pointer must fit in 46-bit payload (64 TB addressable).
        // This is true for all current 64-bit platforms (x86-64: 47-bit user space,
        // ARM64: 48-bit). If it ever fires, we need to box the pointer on heap.
        assert((reinterpret_cast<uint64_t>(static_cast<void*>(obj)) & ~UINT64_C(0x3FFFFFFFFFFF)) == 0
               && "GcObject allocated above 46-bit addressable range — NaN-boxing invariant broken");
        obj->gcNext = head_;
        head_ = obj;
        objectCount_++;
        allocatedBytes_ += obj->gcSize();
        if (allocatedBytes_ >= nextGCThreshold_) {
            pendingGC_ = true;
        }
        return obj;
    }

    /// @brief Allocate a GC-managed object and wrap it in a GcPtr<T>.
    ///
    /// Convenience wrapper around allocate<T>() that returns a GcPtr<T>
    /// instead of a raw pointer.
    ///
    /// @tparam T    The GcObject subclass to allocate.
    /// @tparam Args Constructor argument types (deduced).
    /// @param args  Arguments forwarded to T's constructor.
    /// @return      GcPtr<T> owning a reference to the new object.
    // =========================================================================
    template <typename T, typename... Args>
    GcPtr<T> alloc(Args&&... args) {
        return GcPtr<T>(allocate<T>(std::forward<Args>(args)...));
    }

    // =========================================================================
    /// @brief Run a full mark-sweep garbage collection cycle.
    ///
    /// <b>Algorithm:</b>
    ///   1. <b>Mark phase</b> — Iterate over every GcObject* in the @p roots
    ///      vector and call GcHeap::mark() on each, which transitively traces
    ///      the reachable object graph via GcObject::trace().  Tracing uses a
    ///      worklist (not recursion) so deep object graphs do not overflow
    ///      the C++ call stack.
    ///   2. <b>Sweep phase</b> — Walk the intrusive allocation list.  Objects
    ///      that were NOT marked during the mark phase are deleted and
    ///      removed from the list.  Objects that WERE marked have their mark
    ///      bit cleared for the next cycle.
    ///
    /// After collection, the pending GC flag is cleared and the GC threshold
    /// is adjusted heuristically to avoid thrashing.
    ///
    /// @param roots Vector of root pointers — every object reachable from
    ///              these roots will survive the collection.  The VM supplies
    ///              its value stack, global table, call frames, and any
    ///              temporary registers as roots.
    ///
    /// @see mark(), sweep(), GcObject::trace()
    // =========================================================================
    void collect(const std::vector<GcObject*>& roots);

    /// @brief Check whether a GC cycle is pending.
    /// @return true if allocated bytes have exceeded the threshold and a
    ///         collection has not yet run.
    bool needsGC() const { return pendingGC_; }

    // =========================================================================
    /// @brief Run a GC cycle only if one is pending (convenience shortcut).
    ///
    /// Equivalent to:
    /// @code
    ///   if (needsGC()) collect(roots);
    /// @endcode
    ///
    /// @param roots The current root set (see collect()).
    // =========================================================================
    void collectIfNeeded(const std::vector<GcObject*>& roots) {
        if (pendingGC_) collect(roots);
    }

    /// @brief Return the number of live GC-managed objects.
    size_t objectCount() const { return objectCount_; }

    /// @brief Return the total allocated bytes (sum of all GcObject::gcSize() calls).
    size_t allocatedBytes() const { return allocatedBytes_; }

    /// @brief Return the current GC threshold in bytes.
    ///
    /// When allocatedBytes_ reaches this value, a GC cycle is triggered.
    /// The threshold is adjusted after each collection based on the live
    /// set size to balance memory usage against collection frequency.
    size_t gcThreshold() const { return nextGCThreshold_; }

    /// @brief Reset the heap: delete all tracked objects and clear counters.
    ///
    /// Walks the allocation list from head_ to tail, deleting every object.
    /// After this call the heap is empty (zero objects, zero bytes, no
    /// pending GC).  Used during VM shutdown or test teardown.
    void reset();

    /// @brief Return the head of the internal allocation list.
    /// @note Intended for debugging and testing; callers should not mutate
    ///       the list directly.
    GcObject* head() const { return head_; }

private:
    /// @brief Private constructor — use GcHeap::instance() to obtain the singleton.
    GcHeap() = default;

    /// @brief Destructor calls reset() to clean up all tracked objects.
    ~GcHeap() { reset(); }

    /// @brief Deleted copy constructor — singleton, not copyable.
    GcHeap(const GcHeap&) = delete;

    /// @brief Deleted copy-assignment — singleton, not assignable.
    GcHeap& operator=(const GcHeap&) = delete;

    /** @brief Head of the intrusive singly-linked allocation list.
     *
     *  Every GcObject allocated via allocate() is prepended to this list
     *  through its GcObject::gcNext pointer.  The sweep phase walks this
     *  list to identify and delete unreachable objects. */
    GcObject* head_ = nullptr;

    /** @brief Number of live objects currently tracked by the heap. */
    size_t objectCount_ = 0;

    /** @brief Sum of GcObject::gcSize() for all live objects (bytes). */
    size_t allocatedBytes_ = 0;

    /** @brief Allocation threshold that triggers the next GC cycle (bytes).
     *
     *  Default: 4 MiB.  When allocatedBytes_ >= nextGCThreshold_, the
     *  pendingGC_ flag is set.  The threshold is dynamically adjusted after
     *  each collection: if the live set after GC is still large, the
     *  threshold is raised to avoid frequent collections (thrashing). */
    size_t nextGCThreshold_ = 4 * 1024 * 1024;

    /** @brief Flag indicating a GC cycle should run at the next safe point. */
    bool pendingGC_ = false;

    // =========================================================================
    /// @brief Mark a single object and transitively trace every object
    ///        reachable from it.
    ///
    /// Uses an explicit worklist (the @p worklist vector parameter,
    /// passed by reference and used as a LIFO stack) to avoid recursion.
    /// For each newly marked object, GcObject::trace() is called to push
    /// its immediate children onto the worklist, and the loop continues
    /// until the worklist is exhausted.
    ///
    /// @param obj      Starting object to mark (must be non-null and unmarked).
    /// @param worklist Shared LIFO worklist for the entire mark phase.
    ///                 Objects are pushed here by GcObject::trace() and
    ///                 popped by the mark loop in collect().
    ///
    /// @see GcObject::trace(), collect()
    // =========================================================================
    void mark(GcObject* obj, std::vector<GcObject*>& worklist);

    // =========================================================================
    /// @brief Sweep phase: delete every unmarked object from the allocation list.
    ///
    /// Walks the intrusive list from head_ to tail.  For each object:
    ///   - If NOT marked: the object is unreachable.  It is unlinked from
    ///     the list, deleted, and the object/byte counters are decremented.
    ///   - If marked: the object survived.  Its mark bit is cleared so it
    ///     is ready for the next GC cycle, and it stays in the list.
    ///
    /// @see collect()
    // =========================================================================
    void sweep();
};

} // namespace vora
