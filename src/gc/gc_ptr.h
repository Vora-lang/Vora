#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vora {

/**
 * @file src/gc/gc_ptr.h
 * @brief Non-owning smart pointer for referencing objects managed by the
 *        Vora garbage-collected heap.
 *
 * GcPtr<T> is the primary mechanism by which the VM and runtime components
 * hold references to GC-allocated objects (arrays, dicts, strings, callables,
 * object instances, class definitions, etc.).  It models a raw pointer with
 * explicit GC semantics rather than ownership semantics — the GC heap is the
 * sole owner of the underlying storage; GcPtr merely points into it.
 *
 * @details
 * ## Ownership model
 *
 * All objects allocated by the GC are **owned by the GC heap**.  GcPtr<T>
 * is a **non-owning** reference — it never calls `delete` or `free`.  When
 * the GC determines that an object is unreachable, it deallocates the object
 * independently of any GcPtr instances that may still point to it (those
 * pointers become dangling and must not be dereferenced — the VM guarantees
 * this invariant through correct root registration).
 *
 * ## Write barrier
 *
 * In a generational or incremental garbage collector, the **write barrier**
 * is the mechanism that tracks pointer mutations so the collector can
 * accurately trace the object graph without scanning the entire heap on
 * every cycle.  The write barrier fires whenever a pointer field is
 * **written** (not read).
 *
 * In Vora, the write barrier is **not** implemented inside GcPtr itself.
 * GcPtr stores a raw `T*` and provides only `const` access to it — the
 * pointer member `ptr_` is private and has no mutator method.  This means
 * GcPtr instances are **immutable after construction**: they point to one
 * object for their entire lifetime.  The GC therefore only needs to scan
 * GcPtr values that are alive on the VM stack, in the constant pool, or in
 * reachable heap objects (e.g., Array elements, Dict entries, ObjectInstance
 * fields).  Those heap objects implement their own write barriers when their
 * contents are modified (for example, `Array::set()` or `Dict::insert()`).
 *
 * This design keeps GcPtr lightweight — exactly the size of a raw pointer
 * (typically 8 bytes on 64-bit systems) — and trivially copyable, which
 * makes it suitable for storage in `std::variant` (as used by the `Value`
 * type) and for pass-by-value throughout the VM.
 *
 * @tparam T The type of the GC-managed object being referenced.
 */

// =========================================================================
// GcPtr<T> — non-owning pointer to a GC-managed object.
//
// Behaves like a raw T* but with explicit semantics: the GC heap owns the
// object, GcPtr just references it.  Supports:
//   - null initialization
//   - pointer-like dereference (operator->, operator*)
//   - equality comparison (pointer identity)
//   - storage in std::variant (trivially copyable, same size as void*)
// =========================================================================

template <typename T>
class GcPtr {
public:
    /**
     * @brief Default constructor.  Initializes a null GcPtr.
     */
    GcPtr() : ptr_(nullptr) {}

    /**
     * @brief Constructs a null GcPtr from nullptr.
     * @param[in] unused Always nullptr.
     */
    GcPtr(std::nullptr_t) : ptr_(nullptr) {}

    /**
     * @brief Constructs a GcPtr from a raw pointer.
     *
     * The raw pointer is assumed to point to a valid GC-managed object (or
     * be nullptr).  No ownership transfer takes place.
     *
     * @param p The raw pointer to wrap.
     */
    explicit GcPtr(T* p) : ptr_(p) {}

    /**
     * @brief Implicit conversion from GcPtr of a derived type to GcPtr of
     *        a base type.
     *
     * Enabled only when @p U is derived from @p T (i.e., std::is_base_of_v<T, U>
     * is true).
     *
     * @tparam U The derived type.
     * @param other The GcPtr<U> to convert from.
     */
    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    GcPtr(const GcPtr<U>& other) : ptr_(other.get()) {}

    /**
     * @brief Returns the raw pointer value.
     * @return const pointer to T (or nullptr).
     */
    T* get() const { return ptr_; }

    /**
     * @brief Member access operator.
     *
     * Throws if the pointer is null, providing a clear diagnostic rather than
     * a segfault.
     *
     * @return Pointer to T.
     * @throws std::runtime_error if the GcPtr is null.
     */
    T* operator->() const { if (!ptr_) throw std::runtime_error("GcPtr dereference: pointer is null"); return ptr_; }

    /**
     * @brief Dereference operator.
     *
     * Throws if the pointer is null, providing a clear diagnostic rather than
     * a segfault.
     *
     * @return Reference to T.
     * @throws std::runtime_error if the GcPtr is null.
     */
    T& operator*() const { if (!ptr_) throw std::runtime_error("GcPtr dereference: pointer is null"); return *ptr_; }

    /**
     * @brief Boolean conversion for null checks.
     * @return true if the pointer is non-null, false otherwise.
     */
    explicit operator bool() const { return ptr_ != nullptr; }

    /**
     * @brief Equality comparison between two GcPtr instances.
     *
     * Compares by pointer identity — two GcPtrs are equal only if they
     * point to exactly the same object.
     *
     * @param other The GcPtr to compare against.
     * @return true if both point to the same object.
     */
    bool operator==(const GcPtr& other) const { return ptr_ == other.ptr_; }

    /**
     * @brief Inequality comparison between two GcPtr instances.
     * @param other The GcPtr to compare against.
     * @return true if they point to different objects.
     */
    bool operator!=(const GcPtr& other) const { return ptr_ != other.ptr_; }

    /**
     * @brief Equality comparison against nullptr.
     * @return true if the GcPtr is null.
     */
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }

    /**
     * @brief Inequality comparison against nullptr.
     * @return true if the GcPtr is non-null.
     */
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    /**
     * @brief Equality comparison against a raw pointer.
     *
     * Useful during migration from raw pointers to GcPtr.
     *
     * @param p The raw pointer to compare against.
     * @return true if both are equal (same address or both null).
     */
    bool operator==(const T* p) const { return ptr_ == p; }

    /**
     * @brief Inequality comparison against a raw pointer.
     * @param p The raw pointer to compare against.
     * @return true if they differ.
     */
    bool operator!=(const T* p) const { return ptr_ != p; }

private:
    T* ptr_ = nullptr;  ///< The raw pointer to the GC-managed object.
};

} // namespace vora
