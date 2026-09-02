//==- llvm/ADT/IntrusiveRefCntPtr.h - Smart Refcounting Pointer --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the RefCountedBase, ThreadSafeRefCountedBase, and
/// IntrusiveRefCntPtr classes.
///
/// IntrusiveRefCntPtr is a smart pointer to an object which maintains a
/// reference count.  (ThreadSafe)RefCountedBase is a mixin class that adds a
/// refcount member variable and methods for updating the refcount.  An object
/// that inherits from (ThreadSafe)RefCountedBase deletes itself when its
/// refcount hits zero.
///
/// For example:
///
/// ```
///   class MyClass : public RefCountedBase<MyClass> {};
///
///   void foo() {
///     // Constructing an IntrusiveRefCntPtr increases the pointee's refcount
///     // by 1 (from 0 in this case).
///     IntrusiveRefCntPtr<MyClass> Ptr1(new MyClass());
///
///     // Copying an IntrusiveRefCntPtr increases the pointee's refcount by 1.
///     IntrusiveRefCntPtr<MyClass> Ptr2(Ptr1);
///
///     // Constructing an IntrusiveRefCntPtr has no effect on the object's
///     // refcount.  After a move, the moved-from pointer is null.
///     IntrusiveRefCntPtr<MyClass> Ptr3(std::move(Ptr1));
///     assert(Ptr1 == nullptr);
///
///     // Clearing an IntrusiveRefCntPtr decreases the pointee's refcount by 1.
///     Ptr2.reset();
///
///     // The object deletes itself when we return from the function, because
///     // Ptr3's destructor decrements its refcount to 0.
///   }
/// ```
///
/// You can use IntrusiveRefCntPtr with isa<T>(), dyn_cast<T>(), etc.:
///
/// ```
///   IntrusiveRefCntPtr<MyClass> Ptr(new MyClass());
///   OtherClass *Other = dyn_cast<OtherClass>(Ptr);  // Ptr.get() not required
/// ```
///
/// IntrusiveRefCntPtr works with any class that
///
///  - inherits from (ThreadSafe)RefCountedBase,
///  - has Retain() and Release() methods, or
///  - specializes IntrusiveRefCntPtrInfo.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTRUSIVEREFCNTPTR_H
#define LLVM_ADT_INTRUSIVEREFCNTPTR_H

#include "llvm/Support/Compiler.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>

namespace llvm {

/// A CRTP mixin class that adds reference counting to a type.
///
/// The lifetime of an object which inherits from RefCountedBase is managed by
/// calls to Release() and Retain(), which increment and decrement the object's
/// refcount, respectively.  When a Release() call decrements the refcount to 0,
/// the object deletes itself.
template <class Derived> class RefCountedBase {
  mutable unsigned RefCount = 0;

protected:
  /// Initialize with a reference count of zero.
  RefCountedBase() = default;
  /// Copy construction does not copy the reference count.
  RefCountedBase(const RefCountedBase &) {}
  /// Copy assignment is deleted; reference counts are not copied.
  RefCountedBase &operator=(const RefCountedBase &) = delete;

#ifndef NDEBUG
  /// Destroy; asserts that the reference count is zero.
  ~RefCountedBase() {
    assert(RefCount == 0 &&
           "Destruction occurred when there are still references to this.");
  }
#else
  // Default the destructor in release builds, A trivial destructor may enable
  // better codegen.
  /// Destroy when no references remain (trivial in release builds).
  ~RefCountedBase() = default;
#endif

public:
  /// Return the current reference count.
  unsigned UseCount() const { return RefCount; }

  /// Increment the reference count.
  void Retain() const { ++RefCount; }

  /// Decrement the reference count; delete the object when it reaches zero.
  void Release() const {
    assert(RefCount > 0 && "Reference count is already zero.");
    if (--RefCount == 0)
      delete static_cast<const Derived *>(this);
  }
};

/// A thread-safe version of \c RefCountedBase.
template <class Derived> class ThreadSafeRefCountedBase {
  mutable std::atomic<int> RefCount{0};

protected:
  /// Initialize with a reference count of zero.
  ThreadSafeRefCountedBase() = default;
  /// Copy construction does not copy the reference count.
  ThreadSafeRefCountedBase(const ThreadSafeRefCountedBase &) {}
  /// Copy assignment is deleted; reference counts are not copied.
  ThreadSafeRefCountedBase &
  operator=(const ThreadSafeRefCountedBase &) = delete;

#ifndef NDEBUG
  /// Assert that no references remain when the object is destroyed.
  ~ThreadSafeRefCountedBase() {
    assert(RefCount == 0 &&
           "Destruction occurred when there are still references to this.");
  }
#else
  // Default the destructor in release builds, A trivial destructor may enable
  // better codegen.
  ~ThreadSafeRefCountedBase() = default;
#endif

public:
  /// Return the current reference count.
  unsigned UseCount() const { return RefCount.load(std::memory_order_relaxed); }

  /// Increment the reference count.
  void Retain() const { RefCount.fetch_add(1, std::memory_order_relaxed); }

  /// Atomically decrement the reference count; delete the object when it reaches zero.
  void Release() const {
    int NewRefCount = RefCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
    assert(NewRefCount >= 0 && "Reference count was already zero.");
    if (NewRefCount == 0)
      delete static_cast<const Derived *>(this);
  }
};

/// Class you can specialize to provide custom retain/release functionality for
/// a type.
///
/// Usually specializing this class is not necessary, as IntrusiveRefCntPtr
/// works with any type which defines Retain() and Release() functions -- you
/// can define those functions yourself if RefCountedBase doesn't work for you.
///
/// One case when you might want to specialize this type is if you have
///  - Foo.h defines type Foo and includes Bar.h, and
///  - Bar.h uses IntrusiveRefCntPtr<Foo> in inline functions.
///
/// Because Foo.h includes Bar.h, Bar.h can't include Foo.h in order to pull in
/// the declaration of Foo.  Without the declaration of Foo, normally Bar.h
/// wouldn't be able to use IntrusiveRefCntPtr<Foo>, which wants to call
/// T::Retain and T::Release.
///
/// To resolve this, Bar.h could include a third header, FooFwd.h, which
/// forward-declares Foo and specializes IntrusiveRefCntPtrInfo<Foo>.  Then
/// Bar.h could use IntrusiveRefCntPtr<Foo>, although it still couldn't call any
/// functions on Foo itself, because Foo would be an incomplete type.
template <typename T> struct IntrusiveRefCntPtrInfo {
  /// Return the reference count of \p obj.
  static unsigned useCount(const T *obj) { return obj->UseCount(); }
  /// Increment the reference count of \p obj.
  static void retain(T *obj) { obj->Retain(); }
  /// Decrement the reference count of \p obj.
  static void release(T *obj) { obj->Release(); }
};

/// A smart pointer to a reference-counted object that inherits from
/// RefCountedBase or ThreadSafeRefCountedBase.
///
/// This class increments its pointee's reference count when it is created, and
/// decrements its refcount when it's destroyed (or is changed to point to a
/// different object).
template <typename T> class LLVM_ATTRIBUTE_WARN_UNUSED IntrusiveRefCntPtr {
  T *Obj = nullptr;

public:
  /// Type of the managed object.
  using element_type = T;

  /// Construct an empty pointer that does not retain any object.
  explicit IntrusiveRefCntPtr() = default;
  /// Construct from a raw pointer and retain it if non-null.
  /// @param obj Object to retain, or null.
  IntrusiveRefCntPtr(T *obj) : Obj(obj) { retain(); }
  /// Copy and retain the pointee from \p S.
  IntrusiveRefCntPtr(const IntrusiveRefCntPtr &S) : Obj(S.Obj) { retain(); }
  /// Take ownership of \p S's pointee without retaining.
  IntrusiveRefCntPtr(IntrusiveRefCntPtr &&S) : Obj(S.Obj) { S.Obj = nullptr; }

  /// Take ownership of a convertible IntrusiveRefCntPtr \p S without retaining.
  /// @param S Source smart pointer (left null).
  template <class X,
            std::enable_if_t<std::is_convertible<X *, T *>::value, bool> = true>
  IntrusiveRefCntPtr(IntrusiveRefCntPtr<X> S) : Obj(S.get()) {
    S.Obj = nullptr;
  }

  /// Take ownership from a convertible \c unique_ptr and retain the object.
  /// @param S Unique pointer whose ownership is released into this smart pointer.
  template <class X,
            std::enable_if_t<std::is_convertible<X *, T *>::value, bool> = true>
  IntrusiveRefCntPtr(std::unique_ptr<X> S) : Obj(S.release()) {
    retain();
  }

  /// Release the pointee if non-null.
  ~IntrusiveRefCntPtr() { release(); }

  /// Copy-assign via swap with \p S.
  IntrusiveRefCntPtr &operator=(IntrusiveRefCntPtr S) {
    swap(S);
    return *this;
  }

  /// Dereference the pointee.
  T &operator*() const { return *Obj; }
  /// Return a pointer to the pointee.
  T *operator->() const { return Obj; }
  /// Return the raw pointer without affecting the reference count.
  T *get() const { return Obj; }
  /// Return true if this pointer is non-null.
  explicit operator bool() const { return Obj; }

  /// Exchange pointees with \p other.
  void swap(IntrusiveRefCntPtr &other) {
    T *tmp = other.Obj;
    other.Obj = Obj;
    Obj = tmp;
  }

  /// Release the current pointee and set to null.
  void reset() {
    release();
    Obj = nullptr;
  }

  /// Clear the pointer without decrementing the pointee's reference count.
  void resetWithoutRelease() { Obj = nullptr; }

  /// Return the pointee's reference count, or zero if empty.
  unsigned useCount() const {
    return Obj ? IntrusiveRefCntPtrInfo<T>::useCount(Obj) : 0;
  }

private:
  void retain() {
    if (Obj)
      IntrusiveRefCntPtrInfo<T>::retain(Obj);
  }

  void release() {
    if (Obj)
      IntrusiveRefCntPtrInfo<T>::release(Obj);
  }

  template <typename X> friend class IntrusiveRefCntPtr;
};

/// Return true if \p A and \p B point to the same object.
template <class T, class U>
inline bool operator==(const IntrusiveRefCntPtr<T> &A,
                       const IntrusiveRefCntPtr<U> &B) {
  return A.get() == B.get();
}

/// Return true if \p A and \p B point to different objects.
template <class T, class U>
inline bool operator!=(const IntrusiveRefCntPtr<T> &A,
                       const IntrusiveRefCntPtr<U> &B) {
  return A.get() != B.get();
}

/// Return true if \p A points to the same object as raw pointer \p B.
template <class T, class U>
inline bool operator==(const IntrusiveRefCntPtr<T> &A, U *B) {
  return A.get() == B;
}

/// Return true if \p A points to a different object than raw pointer \p B.
template <class T, class U>
inline bool operator!=(const IntrusiveRefCntPtr<T> &A, U *B) {
  return A.get() != B;
}

/// Return true if raw pointer \p A equals \p B's pointee.
template <class T, class U>
inline bool operator==(T *A, const IntrusiveRefCntPtr<U> &B) {
  return A == B.get();
}

/// Return true if raw pointer \p A differs from \p B's pointee.
template <class T, class U>
inline bool operator!=(T *A, const IntrusiveRefCntPtr<U> &B) {
  return A != B.get();
}

/// Return true if \p B is empty.
template <class T>
bool operator==(std::nullptr_t, const IntrusiveRefCntPtr<T> &B) {
  return !B;
}

/// Return true if \p A is empty.
template <class T>
bool operator==(const IntrusiveRefCntPtr<T> &A, std::nullptr_t B) {
  return B == A;
}

/// Return true if \p B is non-empty.
template <class T>
bool operator!=(std::nullptr_t A, const IntrusiveRefCntPtr<T> &B) {
  return !(A == B);
}

/// Return true if \p A is non-empty.
template <class T>
bool operator!=(const IntrusiveRefCntPtr<T> &A, std::nullptr_t B) {
  return !(A == B);
}

// Make IntrusiveRefCntPtr work with dyn_cast, isa, and the other idioms from
// Casting.h.
template <typename From> struct simplify_type;

template <class T> struct simplify_type<IntrusiveRefCntPtr<T>> {
  using SimpleType = T *;

  static SimpleType getSimplifiedValue(IntrusiveRefCntPtr<T> &Val) {
    return Val.get();
  }
};

template <class T> struct simplify_type<const IntrusiveRefCntPtr<T>> {
  using SimpleType = /*const*/ T *;

  static SimpleType getSimplifiedValue(const IntrusiveRefCntPtr<T> &Val) {
    return Val.get();
  }
};

/// Factory function for creating intrusive ref counted pointers.
template <typename T, typename... Args>
IntrusiveRefCntPtr<T> makeIntrusiveRefCnt(Args &&...A) {
  return IntrusiveRefCntPtr<T>(new T(std::forward<Args>(A)...));
}

} // end namespace llvm

#endif // LLVM_ADT_INTRUSIVEREFCNTPTR_H
