#include "gc_heap.h"

#include <algorithm>
#include "../runtime/value.h"

#ifdef VORA_GC_TRACE
#include <iostream>
#endif

namespace vora {

// =========================================================================
// extractGcObject — extract raw GcObject* from a NaN-boxed Value
// =========================================================================

GcObject* extractGcObject(const Value& v) {
    if (v.isObject()) {
        return v.asObject();
    }
    return nullptr;
}

// =========================================================================
// writeBarrier — record old→young cross-generational references
// =========================================================================

void writeBarrier(GcObject* src, const Value& dst) {
    if (!src || !src->isOld()) return;  // src must be old
    GcObject* dstObj = extractGcObject(dst);
    if (dstObj && dstObj->age_ == 0) {  // dst must be young (eden)
        GcHeap::instance().addToRememberedSet(src);
    }
}

// =========================================================================
// GcHeap::mark — mark a single object and begin its transitive trace
// =========================================================================

void GcHeap::mark(GcObject* obj, std::vector<GcObject*>& worklist, bool isMinor) {
    if (!obj || obj->isMarked()) return;

    // During minor GC, skip old objects — they're already known-live
    if (isMinor && obj->isOld()) return;

    obj->setMarked(true);
    worklist.push_back(obj);
}

// =========================================================================
// GcHeap::collectMinor — young-generation collection
// =========================================================================

void GcHeap::collectMinor(const std::vector<GcObject*>& roots) {
    if (objectCount_ == 0) {
        pendingMinorGC_ = false;
        return;
    }

    minorGCCount_++;

#ifdef VORA_GC_TRACE
    size_t beforeCount = objectCount_;
    size_t beforeBytes = allocatedBytes_;
    std::cerr << "[gc] minor collection #" << minorGCCount_ << " start: "
              << objectCount_ << " objects, " << allocatedBytes_ << " bytes, "
              << rememberedSet_.size() << " remembered\n";
#endif

    // ── Mark phase ──
    std::vector<GcObject*> worklist;
    worklist.reserve(roots.size() + rememberedSet_.size() + 256);

    // Mark from roots
    for (GcObject* root : roots) {
        if (root && !root->isMarked()) {
            // During minor GC, mark roots even if old — they're roots
            root->setMarked(true);
            worklist.push_back(root);
        }
    }

    // Mark from remembered set (old objects that may reference young)
    for (GcObject* oldObj : rememberedSet_) {
        if (oldObj && !oldObj->isMarked()) {
            oldObj->setMarked(true);
            worklist.push_back(oldObj);
        }
    }

    // Transitive closure: trace marked objects.
    // When tracing, old objects' children (young refs) are pushed.
    while (!worklist.empty()) {
        GcObject* obj = worklist.back();
        worklist.pop_back();
        // trace() pushes children unconditionally — we filter in the next
        // iteration via isMarked() guard (objects already marked are skipped)
        obj->trace(worklist);
    }

    // ── Promotion phase ──
    // Increment age on all surviving young objects; promote if threshold reached.
    size_t promoted = 0;
    GcObject* curr = head_;
    while (curr) {
        if (curr->isMarked()) {
            if (!curr->isOld()) {
                curr->promote();
                if (curr->isOld()) {
                    promoted++;
                }
            }
        }
        curr = curr->gcNext;
    }
    promotedCount_ += promoted;

    // ── Sweep phase ──
    sweepMinor();
    pendingMinorGC_ = false;
    youngBytesSinceLastMinorGC_ = 0;

    // Prune dead entries from remembered set
    rememberedSet_.erase(
        std::remove_if(rememberedSet_.begin(), rememberedSet_.end(),
            [](GcObject* obj) { return !obj || !obj->isOld(); }),
        rememberedSet_.end());

    // Adjust minor GC threshold to avoid thrashing
    if (promoted == 0 && minorGCThreshold_ < 4 * 1024 * 1024) {
        // No promotion — grow threshold slightly
        minorGCThreshold_ = minorGCThreshold_ * 3 / 2;
    }

#ifdef VORA_GC_TRACE
    size_t swept = beforeCount - objectCount_;
    std::cerr << "[gc] minor collection end: " << objectCount_ << " objects ("
              << swept << " swept, " << promoted << " promoted), "
              << allocatedBytes_ << " bytes, minor threshold "
              << minorGCThreshold_ << "\n";
#endif
}

// =========================================================================
// GcHeap::collectMajor — full-heap collection
// =========================================================================

void GcHeap::collectMajor(const std::vector<GcObject*>& roots) {
    if (objectCount_ == 0) {
        pendingMajorGC_ = false;
        return;
    }

    majorGCCount_++;

#ifdef VORA_GC_TRACE
    size_t beforeCount = objectCount_;
    size_t beforeBytes = allocatedBytes_;
    std::cerr << "[gc] major collection #" << majorGCCount_ << " start: "
              << objectCount_ << " objects, " << allocatedBytes_ << " bytes\n";
#endif

    // ── Mark phase (all generations) ──
    std::vector<GcObject*> worklist;
    worklist.reserve(roots.size() + 256);

    for (GcObject* root : roots) {
        if (root && !root->isMarked()) {
            root->setMarked(true);
            worklist.push_back(root);
        }
    }

    while (!worklist.empty()) {
        GcObject* obj = worklist.back();
        worklist.pop_back();
        obj->trace(worklist);
    }

    // ── Age all survivors to old generation ──
    GcObject* curr = head_;
    while (curr) {
        if (curr->isMarked()) {
            curr->setAge(PROMOTION_THRESHOLD);  // promote to old
        }
        curr = curr->gcNext;
    }

    // ── Sweep phase ──
    sweepMajor();
    pendingMajorGC_ = false;
    pendingMinorGC_ = false;
    youngBytesSinceLastMinorGC_ = 0;

    // Clear remembered set
    clearRememberedSet();

    // Dynamic threshold adjustment (same as before)
    if (allocatedBytes_ > nextGCThreshold_ / 2) {
        nextGCThreshold_ = allocatedBytes_ * 2;
    }
    if (nextGCThreshold_ < 4 * 1024 * 1024) {
        nextGCThreshold_ = 4 * 1024 * 1024;
    }

#ifdef VORA_GC_TRACE
    size_t swept = beforeCount - objectCount_;
    std::cerr << "[gc] major collection end: " << objectCount_ << " objects ("
              << swept << " swept), " << allocatedBytes_
              << " bytes, next major threshold " << nextGCThreshold_ << "\n";
#endif
}

// =========================================================================
// GcHeap::sweepMinor — sweep only young (unmarked) objects
// =========================================================================

void GcHeap::sweepMinor() {
    GcObject** prev = &head_;
    GcObject* curr = head_;
    size_t swept = 0;
    size_t sweptBytes = 0;

    while (curr) {
        GcObject* next = curr->gcNext;
        if (curr->isMarked()) {
            // Survived — clear mark for next cycle
            curr->setMarked(false);
            *prev = curr;
            prev = &curr->gcNext;
        } else if (curr->isOld()) {
            // Old and unmarked during minor GC — this should not normally
            // happen (old objects are not traced during minor GC, so they
            // remain unmarked).  Old objects are always considered live.
            // Keep them, but clear any stale mark.
            curr->setMarked(false);
            *prev = curr;
            prev = &curr->gcNext;
        } else {
            // Young and unmarked — dead, collect it
            sweptBytes += curr->gcSize();
            delete curr;
            swept++;
        }
        curr = next;
    }
    *prev = nullptr;

    objectCount_ -= swept;
    allocatedBytes_ -= sweptBytes;

#ifdef VORA_GC_TRACE
    if (swept > 0) {
        std::cerr << "[gc] minor sweep: " << swept << " objects, "
                  << sweptBytes << " bytes freed\n";
    }
#endif
}

// =========================================================================
// GcHeap::sweepMajor — sweep all unmarked objects (full heap)
// =========================================================================

void GcHeap::sweepMajor() {
    GcObject** prev = &head_;
    GcObject* curr = head_;
    size_t swept = 0;
    size_t sweptBytes = 0;

    while (curr) {
        GcObject* next = curr->gcNext;
        if (curr->isMarked()) {
            curr->setMarked(false);
            *prev = curr;
            prev = &curr->gcNext;
        } else {
            sweptBytes += curr->gcSize();
            delete curr;
            swept++;
        }
        curr = next;
    }
    *prev = nullptr;

    objectCount_ -= swept;
    allocatedBytes_ -= sweptBytes;

#ifdef VORA_GC_TRACE
    if (swept > 0) {
        std::cerr << "[gc] major sweep: " << swept << " objects, "
                  << sweptBytes << " bytes freed\n";
    }
#endif
}

// =========================================================================
// GcHeap::reset — delete all objects and reset counters
// =========================================================================

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
    pendingMinorGC_ = false;
    pendingMajorGC_ = false;
    youngBytesSinceLastMinorGC_ = 0;
    nextGCThreshold_ = 4 * 1024 * 1024;
    minorGCThreshold_ = 512 * 1024;
    minorGCCount_ = 0;
    majorGCCount_ = 0;
    promotedCount_ = 0;
    rememberedSet_.clear();
}

} // namespace vora
