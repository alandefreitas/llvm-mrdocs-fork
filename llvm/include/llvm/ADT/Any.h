//===- Any.h - Generic type erased holder of any type -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  This file provides Any, a non-template class modeled in the spirit of
///  std::any.  The idea is to provide a type-safe replacement for C's void*.
///  It can hold a value of any copy-constructible copy-assignable type
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ANY_H
#define LLVM_ADT_ANY_H

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/Compiler.h"

#include <cassert>
#include <memory>
#include <type_traits>

namespace llvm {

/// Type-erased holder for a single copy-constructible value.
///
/// Modeled after \c std::any: stores a value of any copy-constructible type
/// behind a type-erased pointer, and recovers it with \c any_cast.
class LLVM_ABI Any {

  // The `Typeid<T>::Id` static data member below is a globally unique
  // identifier for the type `T`. It is explicitly marked with default
  // visibility so that when `-fvisibility=hidden` is used, the loader still
  // merges duplicate definitions across DSO boundaries.
  // We also cannot mark it as `const`, otherwise msvc merges all definitions
  // when lto is enabled, making any comparison return true.
  template <typename T> struct TypeId { static char Id; };

  struct StorageBase {
    virtual ~StorageBase() = default;
    virtual std::unique_ptr<StorageBase> clone() const = 0;
    virtual const void *id() const = 0;
  };

  template <typename T> struct StorageImpl : public StorageBase {
    explicit StorageImpl(const T &Value) : Value(Value) {}

    explicit StorageImpl(T &&Value) : Value(std::move(Value)) {}

    std::unique_ptr<StorageBase> clone() const override {
      return std::make_unique<StorageImpl<T>>(Value);
    }

    const void *id() const override { return &TypeId<T>::Id; }

    T Value;

  private:
    StorageImpl &operator=(const StorageImpl &Other) = delete;
    StorageImpl(const StorageImpl &Other) = delete;
  };

public:
  /// Construct an empty Any with no held value.
  Any() = default;

  /// Copy-construct by cloning the held value, if any.
  Any(const Any &Other)
      : Storage(Other.Storage ? Other.Storage->clone() : nullptr) {}

  /// Construct an Any that holds a decayed copy or move of \p Value.
  ///
  /// Disabled when \p T is Any, when Any is convertible to \p T (to avoid
  /// recursive overload resolution), or when the decayed type is not
  /// copy-constructible.
  template <typename T,
            std::enable_if_t<
                std::conjunction<
                    std::negation<std::is_same<std::decay_t<T>, Any>>,
                    // We also disable this overload when an `Any` object can be
                    // converted to the parameter type because in that case,
                    // this constructor may combine with that conversion during
                    // overload resolution for determining copy
                    // constructibility, and then when we try to determine copy
                    // constructibility below we may infinitely recurse. This is
                    // being evaluated by the standards committee as a potential
                    // DR in `std::any` as well, but we're going ahead and
                    // adopting it to work-around usage of `Any` with types that
                    // need to be implicitly convertible from an `Any`.
                    std::negation<std::is_convertible<Any, std::decay_t<T>>>,
                    std::is_copy_constructible<std::decay_t<T>>>::value,
                int> = 0>
  Any(T &&Value) {
    Storage =
        std::make_unique<StorageImpl<std::decay_t<T>>>(std::forward<T>(Value));
  }

  /// Move-construct, leaving \p Other empty.
  Any(Any &&Other) : Storage(std::move(Other.Storage)) {}

  /// Exchange the held values of this Any and \p Other.
  Any &swap(Any &Other) {
    std::swap(Storage, Other.Storage);
    return *this;
  }

  /// Assign by taking ownership of \p Other's storage (copy-and-swap).
  Any &operator=(Any Other) {
    Storage = std::move(Other.Storage);
    return *this;
  }

  /// Return true if this Any currently holds a value.
  bool has_value() const { return !!Storage; }

  /// Discard the held value, if any, leaving this Any empty.
  void reset() { Storage.reset(); }

private:
  // Only used for the internal llvm::Any implementation
  template <typename T> bool isa() const {
    if (!Storage)
      return false;
    return Storage->id() == &Any::TypeId<remove_cvref_t<T>>::Id;
  }

  /// Extract a copy of the held value as type \p T.
  template <class T> friend T any_cast(const Any &Value);
  /// Extract a copy of the held value as type \p T.
  template <class T> friend T any_cast(Any &Value);
  template <class T> friend T any_cast(Any &&Value);
  template <class T> friend const T *any_cast(const Any *Value);
  template <class T> friend T *any_cast(Any *Value);

  std::unique_ptr<StorageBase> Storage;
};

// Define the type id and initialize with a non-zero value.
// Initializing with a zero value means the variable can end up in either the
// .data or the .bss section. This can lead to multiple definition linker errors
// when some object files are compiled with a compiler that puts the variable
// into .data but they are linked to object files from a different compiler that
// put the variable into .bss. To prevent this issue from happening, initialize
// the variable with a non-zero value, which forces it to land in .data (because
// .bss is zero-initialized).
// See also https://github.com/llvm/llvm-project/issues/62270
template <typename T> char Any::TypeId<T>::Id = 1;

/// Return a copy of the value held in \p Value as type \p T.
///
/// Asserts that \p Value holds a value of (possibly cv-qualified) type \p T.
template <class T> T any_cast(const Any &Value) {
  assert(Value.isa<T>() && "Bad any cast!");
  return static_cast<T>(*any_cast<remove_cvref_t<T>>(&Value));
}

/// Return a copy of the value held in \p Value as type \p T.
///
/// Asserts that \p Value holds a value of (possibly cv-qualified) type \p T.
template <class T> T any_cast(Any &Value) {
  assert(Value.isa<T>() && "Bad any cast!");
  return static_cast<T>(*any_cast<remove_cvref_t<T>>(&Value));
}

/// Move the value held in \p Value out as type \p T.
///
/// Asserts that \p Value holds a value of (possibly cv-qualified) type \p T.
template <class T> T any_cast(Any &&Value) {
  assert(Value.isa<T>() && "Bad any cast!");
  return static_cast<T>(std::move(*any_cast<remove_cvref_t<T>>(&Value)));
}

/// Return a pointer to the held value, or nullptr if the type does not match.
template <class T> const T *any_cast(const Any *Value) {
  using U = remove_cvref_t<T>;
  if (!Value || !Value->isa<U>())
    return nullptr;
  return &static_cast<Any::StorageImpl<U> &>(*Value->Storage).Value;
}

/// Return a pointer to the held value, or nullptr if the type does not match.
template <class T> T *any_cast(Any *Value) {
  using U = std::decay_t<T>;
  if (!Value || !Value->isa<U>())
    return nullptr;
  return &static_cast<Any::StorageImpl<U> &>(*Value->Storage).Value;
}

} // end namespace llvm

#endif // LLVM_ADT_ANY_H
