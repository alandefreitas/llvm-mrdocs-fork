//===- llvm/Support/PointerLikeTypeTraits.h - Pointer Traits ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PointerLikeTypeTraits class.  This allows data
// structures to reason about pointers and other things that are pointer sized.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_POINTERLIKETYPETRAITS_H
#define LLVM_SUPPORT_POINTERLIKETYPETRAITS_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

namespace llvm {

/// A traits type that is used to handle pointer types and things that are just
/// wrappers for pointers as a uniform entity.
template <typename T> struct PointerLikeTypeTraits;

namespace detail {
// Provide a trait to check if T is pointer-like.
template <typename T, typename U = void> struct HasPointerLikeTypeTraits {
  static const bool value = false;
};

// sizeof(T) is valid only for a complete T.
template <typename T>
struct HasPointerLikeTypeTraits<
    T, decltype((sizeof(PointerLikeTypeTraits<T>) + sizeof(T)), void())> {
  static const bool value = true;
};

template <typename T> struct IsPointerLike {
  static const bool value = HasPointerLikeTypeTraits<T>::value;
};

template <typename T> struct IsPointerLike<T *> {
  static const bool value = true;
};
} // namespace detail

/// PointerLikeTypeTraits specialization for non-cvr pointers.
template <typename T> struct PointerLikeTypeTraits<T *> {
  /// Return pointer \p P as an opaque void pointer.
  /// \param P Pointer to convert.
  /// \return Opaque void pointer representation of \p P.
  static inline void *getAsVoidPointer(T *P) { return P; }
  /// Recover a typed pointer from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Typed pointer recovered from \p P.
  static inline T *getFromVoidPointer(void *P) { return static_cast<T *>(P); }

  /// Number of spare low bits available given the pointee's alignment.
  static constexpr int NumLowBitsAvailable = ConstantLog2<alignof(T)>();
};

/// PointerLikeTypeTraits specialization for untyped void pointers.
template <> struct PointerLikeTypeTraits<void *> {
  /// Return void pointer \p P unchanged as an opaque void pointer.
  /// \param P Pointer to convert.
  /// \return Opaque void pointer representation of \p P.
  static inline void *getAsVoidPointer(void *P) { return P; }
  /// Recover a void pointer from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Void pointer recovered from \p P.
  static inline void *getFromVoidPointer(void *P) { return P; }

  /// Number of spare low bits assumed available in a void pointer (two).
  ///
  /// Note, we assume here that void* is related to raw malloc'ed memory and
  /// that malloc returns objects at least 4-byte aligned. However, this may be
  /// wrong, or pointers may be from something other than malloc. In this case,
  /// you should specify a real typed pointer or avoid this template.
  ///
  /// All clients should use assertions to do a run-time check to ensure that
  /// this is actually true.
  static constexpr int NumLowBitsAvailable = 2;
};

/// PointerLikeTypeTraits specialization for const-qualified types.
template <typename T> struct PointerLikeTypeTraits<const T> {
  /// Non-const traits type used to implement this specialization.
  using NonConst = PointerLikeTypeTraits<T>;

  /// Return const value \p P as an opaque const void pointer.
  /// \param P Const pointer-like value to convert.
  /// \return Opaque const void pointer representation of \p P.
  static inline const void *getAsVoidPointer(const T P) {
    return NonConst::getAsVoidPointer(P);
  }
  /// Recover a const value from opaque const void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Const value recovered from \p P.
  static inline const T getFromVoidPointer(const void *P) {
    return NonConst::getFromVoidPointer(const_cast<void *>(P));
  }
  /// Number of spare low bits available, matching the non-const traits.
  static constexpr int NumLowBitsAvailable = NonConst::NumLowBitsAvailable;
};

/// PointerLikeTypeTraits specialization for pointers to const objects.
template <typename T> struct PointerLikeTypeTraits<const T *> {
  /// Non-const pointer traits type used to implement this specialization.
  using NonConst = PointerLikeTypeTraits<T *>;

  /// Return const pointer \p P as an opaque const void pointer.
  /// \param P Pointer to convert.
  /// \return Opaque const void pointer representation of \p P.
  static inline const void *getAsVoidPointer(const T *P) {
    return NonConst::getAsVoidPointer(const_cast<T *>(P));
  }
  /// Recover a const pointer from opaque const void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Const pointer recovered from \p P.
  static inline const T *getFromVoidPointer(const void *P) {
    return NonConst::getFromVoidPointer(const_cast<void *>(P));
  }
  /// Number of spare low bits available, matching the non-const traits.
  static constexpr int NumLowBitsAvailable = NonConst::NumLowBitsAvailable;
};

/// PointerLikeTypeTraits specialization for integer pointer representations.
template <> struct PointerLikeTypeTraits<uintptr_t> {
  /// Reinterpret integer pointer value \p P as an opaque void pointer.
  /// \param P Integer representation of a pointer.
  /// \return Opaque void pointer representation of \p P.
  static inline void *getAsVoidPointer(uintptr_t P) {
    return reinterpret_cast<void *>(P);
  }
  /// Recover an integer pointer value from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Integer pointer value recovered from \p P.
  static inline uintptr_t getFromVoidPointer(void *P) {
    return reinterpret_cast<uintptr_t>(P);
  }
  /// Number of spare low bits available (none; every bit is significant).
  static constexpr int NumLowBitsAvailable = 0;
};

/// Provide suitable custom traits struct for function pointers.
///
/// Function pointers can't be directly given these traits as functions can't
/// have their alignment computed with `alignof` and we need different casting.
///
/// To rely on higher alignment for a specialized use, you can provide a
/// customized form of this template explicitly with higher alignment, and
/// potentially use alignment attributes on functions to satisfy that.
template <int Alignment, typename FunctionPointerT>
struct FunctionPointerLikeTypeTraits {
  /// Low bits free given the assumed function-pointer alignment.
  static constexpr int NumLowBitsAvailable = ConstantLog2<Alignment>();
  /// Reinterpret function pointer \p P as an opaque void pointer.
  /// \param P Function pointer to convert.
  /// \return Opaque void pointer representation of \p P.
  static inline void *getAsVoidPointer(FunctionPointerT P) {
    assert((reinterpret_cast<uintptr_t>(P) &
            ~((uintptr_t)-1 << NumLowBitsAvailable)) == 0 &&
           "Alignment not satisfied for an actual function pointer!");
    return reinterpret_cast<void *>(P);
  }
  /// Recover a function pointer from a void pointer.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return Function pointer recovered from \p P.
  static inline FunctionPointerT getFromVoidPointer(void *P) {
    return reinterpret_cast<FunctionPointerT>(P);
  }
};

/// Provide a default specialization for function pointers that assumes 4-byte
/// alignment.
///
/// We assume here that functions used with this are always at least 4-byte
/// aligned. This means that, for example, thumb functions won't work or systems
/// with weird unaligned function pointers won't work. But all practical systems
/// we support satisfy this requirement.
template <typename ReturnT, typename... ParamTs>
struct PointerLikeTypeTraits<ReturnT (*)(ParamTs...)>
    : FunctionPointerLikeTypeTraits<4, ReturnT (*)(ParamTs...)> {};

} // end namespace llvm

#endif
