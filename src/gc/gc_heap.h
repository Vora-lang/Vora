#pragma once

#include "gc_object.h"
#include "gc_ptr.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vora {

// =========================================================================
// GcHeap — singleton mark-sweep garbage collector.
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
