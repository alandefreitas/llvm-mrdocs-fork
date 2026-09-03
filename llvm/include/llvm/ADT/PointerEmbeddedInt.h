//===- llvm/ADT/PointerEmbeddedInt.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTEREMBEDDEDINT_H
#define LLVM_ADT_POINTEREMBEDDEDINT_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <type_traits>

namespace llvm {

/// Utility to embed an integer into a pointer-like type.
///
/// This is specifically intended to allow embedding integers where fewer bits
/// are required than exist in a pointer, and the integer can participate in
/// abstractions along side other pointer-like types. For example it can be
/// placed into a \c PointerSumType or \c PointerUnion.
///
/// Note that much like pointers, an integer value of zero has special utility
/// due to boolean conversions. For example, a non-null value can be tested for
/// in the above abstractions without testing the particular active member.
/// Also, the default constructed value zero initializes the integer.
template <typename IntT, int Bits = sizeof(IntT) * CHAR_BIT>
class PointerEmbeddedInt {
  uintptr_t Value = 0;

  // Note: This '<' is correct; using '<=' would result in some shifts
  // overflowing their storage types.
  static_assert(Bits < sizeof(uintptr_t) * CHAR_BIT,
                "Cannot embed more bits than we have in a pointer!");

  enum : uintptr_t {
    // We shift as many zeros into the value as we can while preserving the
    // number of bits desired for the integer.
    Shift = sizeof(uintptr_t) * CHAR_BIT - Bits,
  };

  struct RawValueTag {
    explicit RawValueTag() = default;
  };

  friend struct PointerLikeTypeTraits<PointerEmbeddedInt>;

  explicit PointerEmbeddedInt(uintptr_t Value, RawValueTag) : Value(Value) {}

public:
  /// Construct with integer value zero (the empty/default embedded value).
  PointerEmbeddedInt() = default;

  /// Construct by embedding integer \p I into the pointer-like representation.
  /// \param I Integer value to embed.
  PointerEmbeddedInt(IntT I) { *this = I; }

  /// Embed integer \p I, asserting it fits in the reserved \c Bits.
  /// \param I Integer value to embed.
  /// @return Reference to this object after embedding \p I.
  PointerEmbeddedInt &operator=(IntT I) {
    assert((std::is_signed<IntT>::value ? isInt<Bits>(I) : isUInt<Bits>(I)) &&
           "Integer has bits outside those preserved!");
    Value = static_cast<uintptr_t>(I) << Shift;
    return *this;
  }

  // Note that this implicit conversion additionally allows all of the basic
  // comparison operators to work transparently, etc.
  /// Extract the embedded integer value.
  /// @return The integer previously stored via construction or assignment.
  operator IntT() const {
    if (std::is_signed<IntT>::value)
      return static_cast<IntT>(static_cast<intptr_t>(Value) >> Shift);
    return static_cast<IntT>(Value >> Shift);
  }
};

/// PointerLikeTypeTraits specialization treating PointerEmbeddedInt as a
/// pointer.
///
/// Enables use with pointer unions and sum types.
template <typename IntT, int Bits>
struct PointerLikeTypeTraits<PointerEmbeddedInt<IntT, Bits>> {
  /// PointerEmbeddedInt type this traits specialization describes.
  using T = PointerEmbeddedInt<IntT, Bits>;

  /// Return the embedded bit pattern of \p P as a void pointer.
  /// \param P Value to convert.
  /// @return Opaque void pointer encoding the embedded bit pattern.
  static inline void *getAsVoidPointer(const T &P) {
    return reinterpret_cast<void *>(P.Value);
  }

  /// Reconstruct a PointerEmbeddedInt from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// @return PointerEmbeddedInt reconstructed from the opaque bit pattern.
  static inline T getFromVoidPointer(void *P) {
    return T(reinterpret_cast<uintptr_t>(P), typename T::RawValueTag());
  }

  /// Reconstruct a PointerEmbeddedInt from opaque const void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// @return PointerEmbeddedInt reconstructed from the opaque bit pattern.
  static inline T getFromVoidPointer(const void *P) {
    return T(reinterpret_cast<uintptr_t>(P), typename T::RawValueTag());
  }

  /// Number of spare low bits available for nesting after the embedded bits.
  static constexpr int NumLowBitsAvailable = T::Shift;
};

/// DenseMapInfo specialization so PointerEmbeddedInt can be a DenseMap key.
///
/// Requires that DenseMapInfo is available for the underlying IntT type.
template <typename IntT, int Bits>
struct DenseMapInfo<PointerEmbeddedInt<IntT, Bits>> {
  /// PointerEmbeddedInt type this DenseMapInfo specialization describes.
  using T = PointerEmbeddedInt<IntT, Bits>;

  /// Hash \p Arg by extracting and hashing the embedded integer.
  /// \param Arg Key value to hash.
  /// @return Hash of the embedded integer value.
  static unsigned getHashValue(const T &Arg) {
    return DenseMapInfo<IntT>::getHashValue(Arg);
  }

  /// Return true if \p LHS and \p RHS hold equal embedded integers.
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// @return True if both keys hold the same embedded integer.
  static bool isEqual(const T &LHS, const T &RHS) { return LHS == RHS; }
};

} // end namespace llvm

#endif // LLVM_ADT_POINTEREMBEDDEDINT_H
