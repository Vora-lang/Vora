#pragma once

#include <cstddef>
#include <vector>

namespace vora {

// =========================================================================
// GcObject — base class for all garbage-collected heap objects.
//
// Subclasses must implement:
//   trace(worklist) — push all referenced GcObject* onto the worklist
//   gcSize()        — approximate byte size (for heap accounting)
// =========================================================================

class GcObject {
public:
    virtual ~GcObject() = default;

    // Push every GcObject* reachable from `this` onto `worklist`.
    // The GC mark phase uses this to transitively trace the object graph.
    virtual void trace(std::vector<GcObject*>& worklist) = 0;

    // Approximate size in bytes (default: sizeof(*this)).
    virtual size_t gcSize() const { return sizeof(*this); }

    bool isMarked() const { return marked_; }
    void setMarked(bool m) { marked_ = m; }

    // Next pointer for the GC's intrusive linked list of all objects.
    GcObject* gcNext = nullptr;

private:
    bool marked_ = false;
};

} // namespace vora
