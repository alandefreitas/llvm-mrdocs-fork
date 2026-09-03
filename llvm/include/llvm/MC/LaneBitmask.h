//===- llvm/MC/LaneBitmask.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A common definition of LaneBitmask for use in TableGen and CodeGen.
///
/// A lane mask is a bitmask representing the covering of a register with
/// sub-registers.
///
/// This is typically used to track liveness at sub-register granularity.
/// Lane masks for sub-register indices are similar to register units for
/// physical registers. The individual bits in a lane mask can't be assigned
/// any specific meaning. They can be used to check if two sub-register
/// indices overlap.
///
/// Iff the target has a register such that:
///
///   getSubReg(Reg, A) overlaps getSubReg(Reg, B)
///
/// then:
///
///   (getSubRegIndexLaneMask(A) & getSubRegIndexLaneMask(B)) != 0

#ifndef LLVM_MC_LANEBITMASK_H
#define LLVM_MC_LANEBITMASK_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

  /// Bitmask representing which sub-register lanes cover a register.
  struct LaneBitmask {
    // When changing the underlying type, change the format string as well.
    /// Underlying integer type storing the lane mask bits.
    using Type = uint64_t;
    /// Constants describing the size of the lane mask representation.
    enum : unsigned {
      /// Number of bits in the underlying \c Type.
      BitWidth = 8*sizeof(Type)
    };
    /// Printf-style format string for printing a lane mask as hex.
    constexpr static const char *const FormatStr = "%016llX";

    /// Construct an empty lane mask with no sub-register lanes set.
    constexpr LaneBitmask() = default;
    /// Construct a lane mask from the raw integer value \p V.
    /// @param V Bit pattern to store as the lane mask.
    explicit constexpr LaneBitmask(Type V) : Mask(V) {}

    /// Return true if this mask equals \p M.
    /// @param M Lane mask to compare against.
    /// @return True if the masks are equal.
    constexpr bool operator== (LaneBitmask M) const { return Mask == M.Mask; }
    /// Return true if this mask differs from \p M.
    /// @param M Lane mask to compare against.
    /// @return True if the masks differ.
    constexpr bool operator!= (LaneBitmask M) const { return Mask != M.Mask; }
    /// Return true if this mask is less than \p M when compared as integers.
    /// @param M Lane mask to compare against.
    /// @return True if this mask is less than \p M.
    constexpr bool operator< (LaneBitmask M)  const { return Mask < M.Mask; }
    /// Return true if no lane bits are set.
    /// @return True if no lane bits are set.
    constexpr bool none() const { return Mask == 0; }
    /// Return true if any lane bit is set.
    /// @return True if any lane bit is set.
    constexpr bool any()  const { return Mask != 0; }
    /// Return true if every lane bit is set.
    /// @return True if every lane bit is set.
    constexpr bool all()  const { return ~Mask == 0; }

    /// Return a lane mask with every bit inverted.
    /// @return A lane mask with every bit inverted.
    constexpr LaneBitmask operator~() const {
      return LaneBitmask(~Mask);
    }
    /// Return the bitwise OR of this mask and \p M.
    /// @param M Lane mask to combine with.
    /// @return The bitwise OR of this mask and \p M.
    constexpr LaneBitmask operator|(LaneBitmask M) const {
      return LaneBitmask(Mask | M.Mask);
    }
    /// Return the bitwise AND of this mask and \p M.
    /// @param M Lane mask to combine with.
    /// @return The bitwise AND of this mask and \p M.
    constexpr LaneBitmask operator&(LaneBitmask M) const {
      return LaneBitmask(Mask & M.Mask);
    }
    /// Bitwise-OR \p M into this mask and return this.
    /// @param M Lane mask whose set bits are added.
    /// @return This lane mask after the update.
    LaneBitmask &operator|=(LaneBitmask M) {
      Mask |= M.Mask;
      return *this;
    }
    /// Bitwise-AND \p M into this mask and return this.
    /// @param M Lane mask to intersect with.
    /// @return This lane mask after the update.
    LaneBitmask &operator&=(LaneBitmask M) {
      Mask &= M.Mask;
      return *this;
    }

    /// Return the underlying integer representation of this lane mask.
    /// @return The underlying integer representation of this lane mask.
    constexpr Type getAsInteger() const { return Mask; }

    /// Return the number of set lane bits in this mask.
    /// @return The number of set lane bits in this mask.
    unsigned getNumLanes() const { return llvm::popcount(Mask); }
    /// Return the index of the highest set lane bit.
    /// @return The index of the highest set lane bit.
    unsigned getHighestLane() const {
      return Log2_64(Mask);
    }

    /// Return a lane mask with no bits set.
    /// @return A lane mask with no bits set.
    static constexpr LaneBitmask getNone() { return LaneBitmask(0); }
    /// Return a lane mask with every bit set.
    /// @return A lane mask with every bit set.
    static constexpr LaneBitmask getAll() { return ~LaneBitmask(0); }
    /// Return a lane mask with only bit \p Lane set.
    /// @param Lane Zero-based index of the single lane to set.
    /// @return A lane mask with only bit \p Lane set.
    static constexpr LaneBitmask getLane(unsigned Lane) {
      return LaneBitmask(Type(1) << Lane);
    }

  private:
    Type Mask = 0;
  };

  /// Create Printable object to print LaneBitmasks on a \ref raw_ostream.
  /// @param LaneMask Lane mask whose integer value will be formatted.
  /// @return A Printable that formats \p LaneMask as hex on a stream.
  inline Printable PrintLaneMask(LaneBitmask LaneMask) {
    return Printable([LaneMask](raw_ostream &OS) {
      OS << format(LaneBitmask::FormatStr, LaneMask.getAsInteger());
    });
  }

} // end namespace llvm

#endif // LLVM_MC_LANEBITMASK_H
