#pragma once

#include "gc_object.h"
#include "gc_ptr.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vora {

// =========================================================================
// GcHeap — singleton mark-sweep garbage collector.
// Single-threaded by design: the VM runs on one thread. If multi-threading
// is added in the future, this singleton will need synchronization (mutex
// around allocations and GC cycles) or a per-thread heap model.
// =========================================================================

class GcHeap {
public:
    static GcHeap& instance() {
        static GcHeap heap;
        return heap;
    }

    // Allocate a GC-managed object of type T, forwarding args to its ctor.
    // Returns a raw T*; wrap with GcPtr<T>() or use alloc<T>().
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

    // Convenience: allocate + wrap in GcPtr<T>.
    template <typename T, typename... Args>
    GcPtr<T> alloc(Args&&... args) {
        return GcPtr<T>(allocate<T>(std::forward<Args>(args)...));
    }

    // Mark-sweep collection.
    void collect(const std::vector<GcObject*>& roots);

    bool needsGC() const { return pendingGC_; }

    void collectIfNeeded(const std::vector<GcObject*>& roots) {
        if (pendingGC_) collect(roots);
    }

    size_t objectCount() const { return objectCount_; }
    size_t allocatedBytes() const { return allocatedBytes_; }
    size_t gcThreshold() const { return nextGCThreshold_; }

    void reset();
    GcObject* head() const { return head_; }

private:
    GcHeap() = default;
    ~GcHeap() { reset(); }
    GcHeap(const GcHeap&) = delete;
    GcHeap& operator=(const GcHeap&) = delete;

    GcObject* head_ = nullptr;
    size_t objectCount_ = 0;
    size_t allocatedBytes_ = 0;
    size_t nextGCThreshold_ = 4 * 1024 * 1024;
    bool pendingGC_ = false;

    void mark(GcObject* obj, std::vector<GcObject*>& worklist);
    void sweep();
};

} // namespace vora
