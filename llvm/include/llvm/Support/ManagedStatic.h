//===-- llvm/Support/ManagedStatic.h - Static Global wrapper ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ManagedStatic class and the llvm_shutdown() function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MANAGEDSTATIC_H
#define LLVM_SUPPORT_MANAGEDSTATIC_H

#include "llvm/Support/Compiler.h"
#include <atomic>
#include <cstddef>

namespace llvm {

/// object_creator - Helper method for ManagedStatic.
template <class C> struct object_creator {
  /// Allocate and default-construct an instance of \c C.
  ///
  /// \return Opaque pointer to the new instance.
  static void *call() { return new C(); }
};

/// object_deleter - Helper method for ManagedStatic.
///
template <typename T> struct object_deleter {
  /// Delete a single object previously created for a ManagedStatic.
  ///
  /// \param Ptr Pointer to a \c T instance to delete.
  static void call(void *Ptr) { delete (T *)Ptr; }
};
/// Helper that deletes a fixed-size array owned by a ManagedStatic.
template <typename T, size_t N> struct object_deleter<T[N]> {
  /// Delete an array previously created for a ManagedStatic.
  ///
  /// \param Ptr Pointer to a \c T[N] array to delete.
  static void call(void *Ptr) { delete[](T *)Ptr; }
};

// ManagedStatic must be initialized to zero, and it must *not* have a dynamic
// initializer because managed statics are often created while running other
// dynamic initializers. In standard C++11, the best way to accomplish this is
// with a constexpr default constructor. However, different versions of the
// Visual C++ compiler have had bugs where, even though the constructor may be
// constexpr, a dynamic initializer may be emitted depending on optimization
// settings. For the affected versions of MSVC, use the old linker
// initialization pattern of not providing a constructor and leaving the fields
// uninitialized. See http://llvm.org/PR41367 for details.
#if !defined(_MSC_VER) || (_MSC_VER >= 1925) || defined(__clang__)
#define LLVM_USE_CONSTEXPR_CTOR
#endif

/// ManagedStaticBase - Common base class for ManagedStatic instances.
class ManagedStaticBase {
protected:
#ifdef LLVM_USE_CONSTEXPR_CTOR
  /// Pointer to the lazily constructed object, or null if not yet created.
  mutable std::atomic<void *> Ptr{};
  /// Function used to destroy the object pointed to by \c Ptr.
  mutable void (*DeleterFn)(void *) = nullptr;
  /// Next ManagedStatic in the global destruction list.
  mutable const ManagedStaticBase *Next = nullptr;
#else
  // This should only be used as a static variable, which guarantees that this
  // will be zero initialized.
  /// Pointer to the lazily constructed object, or null if not yet created.
  mutable std::atomic<void *> Ptr;
  /// Function used to destroy the object pointed to by \c Ptr.
  mutable void (*DeleterFn)(void *);
  /// Next ManagedStatic in the global destruction list.
  mutable const ManagedStaticBase *Next;
#endif

  /// Register this ManagedStatic for construction and later destruction.
  ///
  /// \param creator Function that allocates and constructs the object.
  /// \param deleter Function that destroys and deallocates the object.
  LLVM_ABI void RegisterManagedStatic(void *(*creator)(),
                                      void (*deleter)(void *)) const;

public:
#ifdef LLVM_USE_CONSTEXPR_CTOR
  /// Construct an uninitialized ManagedStaticBase.
  constexpr ManagedStaticBase() = default;
#endif

  /// isConstructed - Return true if this object has not been created yet.
  ///
  /// \return True if the managed object has already been constructed.
  bool isConstructed() const { return Ptr != nullptr; }

  /// Destroy the managed object and unregister this ManagedStatic.
  LLVM_ABI void destroy() const;
};

/// Lazily constructed global destroyed explicitly by llvm_shutdown().
///
/// This transparently changes the behavior of global statics to be lazily
/// constructed on demand (good for reducing startup times of dynamic libraries
/// that link in LLVM components) and for making destruction be explicit through
/// the llvm_shutdown() function call.
template <class C, class Creator = object_creator<C>,
          class Deleter = object_deleter<C>>
class ManagedStatic : public ManagedStaticBase {
public:
  /// Access the managed object, constructing it on first use.
  ///
  /// \return Reference to the managed object.
  C &operator*() {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(Creator::call, Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  /// Access the managed object through a pointer, constructing it on first use.
  ///
  /// \return Pointer to the managed object.
  C *operator->() { return &**this; }

  /// Access the managed object, constructing it on first use.
  ///
  /// \return Const reference to the managed object.
  const C &operator*() const {
    void *Tmp = Ptr.load(std::memory_order_acquire);
    if (!Tmp)
      RegisterManagedStatic(Creator::call, Deleter::call);

    return *static_cast<C *>(Ptr.load(std::memory_order_relaxed));
  }

  /// Access the managed object through a pointer, constructing it on first use.
  ///
  /// \return Pointer to the managed object.
  const C *operator->() const { return &**this; }

  /// Take ownership of the instance and leave this ManagedStatic uninitialized.
  ///
  /// The user is then responsible for the lifetime of the returned instance.
  ///
  /// \return Pointer to the previously managed instance, or null if none.
  C *claim() {
    return static_cast<C *>(Ptr.exchange(nullptr));
  }
};

/// llvm_shutdown - Deallocate and destroy all ManagedStatic variables.
LLVM_ABI void llvm_shutdown();

/// llvm_shutdown_obj - This is a simple helper class that calls
/// llvm_shutdown() when it is destroyed.
struct llvm_shutdown_obj {
  /// Construct an object that will call llvm_shutdown() on destruction.
  llvm_shutdown_obj() = default;
  /// Call llvm_shutdown() to destroy all ManagedStatic variables.
  ~llvm_shutdown_obj() { llvm_shutdown(); }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_MANAGEDSTATIC_H
