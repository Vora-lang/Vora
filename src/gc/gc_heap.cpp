#include "gc_heap.h"

#ifdef VORA_GC_TRACE
#include <iostream>
#endif

namespace vora {

void GcHeap::collect(const std::vector<GcObject*>& roots) {
    if (objectCount_ == 0) {
        pendingGC_ = false;
        return;
    }

#ifdef VORA_GC_TRACE
    std::cerr << "[gc] collection start: " << objectCount_ << " objects, "
              << allocatedBytes_ << " bytes\n";
#endif

    // ── Mark phase ──
    std::vector<GcObject*> worklist;
    worklist.reserve(roots.size() + 256);

    for (GcObject* root : roots) {
        if (root && !root->isMarked()) {
            root->setMarked(true);
            worklist.push_back(root);
        }
    }

    // Transitive closure: trace each marked object into the worklist.
    while (!worklist.empty()) {
        GcObject* obj = worklist.back();
        worklist.pop_back();
        obj->trace(worklist);
    }

    // ── Sweep phase ──
    sweep();

    pendingGC_ = false;

    // Grow the threshold if the heap is getting large (avoid thrashing).
    if (allocatedBytes_ > nextGCThreshold_ / 2) {
        nextGCThreshold_ = allocatedBytes_ * 2;
    }
    if (nextGCThreshold_ < 4 * 1024 * 1024) {
        nextGCThreshold_ = 4 * 1024 * 1024;
    }

#ifdef VORA_GC_TRACE
    std::cerr << "[gc] collection end: " << objectCount_ << " objects, "
              << allocatedBytes_ << " bytes, next threshold "
              << nextGCThreshold_ << "\n";
#endif
}

void GcHeap::mark(GcObject* obj, std::vector<GcObject*>& worklist) {
    if (!obj || obj->isMarked()) return;
    obj->setMarked(true);
    worklist.push_back(obj);
}

void GcHeap::sweep() {
    // Walk the intrusive linked list.  Unmarked objects are removed.
    // We rebuild the list with only marked objects.
    GcObject** prev = &head_;
    GcObject* curr = head_;
    size_t swept = 0;
    size_t sweptBytes = 0;

    while (curr) {
        GcObject* next = curr->gcNext;
        if (curr->isMarked()) {
            // Keep it — clear mark for next cycle
            curr->setMarked(false);
            *prev = curr;
            prev = &curr->gcNext;
        } else {
            // Dead — delete
            sweptBytes += curr->gcSize();
            delete curr;
            swept++;
        }
        curr = next;
    }
    *prev = nullptr;  // terminate list

    objectCount_ -= swept;
    allocatedBytes_ -= sweptBytes;

#ifdef VORA_GC_TRACE
    if (swept > 0) {
        std::cerr << "[gc] swept " << swept << " objects, "
                  << sweptBytes << " bytes freed\n";
    }
#endif
}

void GcHeap::reset() {
    GcObject* curr = head_;
    while (curr) {
        GcObject* next = curr->gcNext;
        delete curr;
        curr = next;
    }
    head_ = nullptr;
    objectCount_ = 0;
    allocatedBytes_ = 0;
    pendingGC_ = false;
    nextGCThreshold_ = 4 * 1024 * 1024;
}

} // namespace vora
