#pragma once

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace vora {

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
    GcPtr() : ptr_(nullptr) {}
    GcPtr(std::nullptr_t) : ptr_(nullptr) {}
    explicit GcPtr(T* p) : ptr_(p) {}

    // Allow implicit conversion from GcPtr<Derived> to GcPtr<Base>
    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    GcPtr(const GcPtr<U>& other) : ptr_(other.get()) {}

    T* get() const { return ptr_; }
    T* operator->() const { assert(ptr_ && "GcPtr dereference: pointer is null"); return ptr_; }
    T& operator*() const { assert(ptr_ && "GcPtr dereference: pointer is null"); return *ptr_; }

    explicit operator bool() const { return ptr_ != nullptr; }

    bool operator==(const GcPtr& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const GcPtr& other) const { return ptr_ != other.ptr_; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    // Allow comparison with raw T* (useful in migration)
    bool operator==(const T* p) const { return ptr_ == p; }
    bool operator!=(const T* p) const { return ptr_ != p; }

private:
    T* ptr_ = nullptr;
};

} // namespace vora
