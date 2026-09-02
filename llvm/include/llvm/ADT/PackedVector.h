//===- llvm/ADT/PackedVector.h - Packed values vector -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the PackedVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_PACKEDVECTOR_H
#define LLVM_ADT_PACKEDVECTOR_H

#include "llvm/ADT/BitVector.h"
#include <cassert>
#include <limits>

namespace llvm {

/// Store a vector of values using a specific number of bits for each
/// value. Both signed and unsigned types can be used, e.g
/// @code
///   PackedVector<signed, 2> vec;
/// @endcode
/// will create a vector accepting values -2, -1, 0, 1. Any other value will hit
/// an assertion.
template <typename T, unsigned BitNum, typename BitVectorTy = BitVector>
class PackedVector {
  static_assert(BitNum > 0, "BitNum must be > 0");

  BitVectorTy Bits;
  // Keep track of the number of elements on our own.
  // We always maintain Bits.size() == NumElements * BitNum.
  // Used to avoid an integer division in size().
  unsigned NumElements = 0;

  static T getValue(const BitVectorTy &Bits, unsigned Idx) {
    if constexpr (std::numeric_limits<T>::is_signed) {
      T val = T();
      for (unsigned i = 0; i != BitNum - 1; ++i)
        val = T(val | ((Bits[(Idx * BitNum) + i] ? 1UL : 0UL) << i));
      if (Bits[(Idx * BitNum) + BitNum - 1])
        val = ~val;
      return val;
    } else {
      T val = T();
      for (unsigned i = 0; i != BitNum; ++i)
        val = T(val | ((Bits[(Idx * BitNum) + i] ? 1UL : 0UL) << i));
      return val;
    }
  }

  static void setValue(BitVectorTy &Bits, unsigned Idx, T val) {
    if constexpr (std::numeric_limits<T>::is_signed) {
      if (val < 0) {
        val = ~val;
        Bits.set((Idx * BitNum) + BitNum - 1);
      } else {
        Bits.reset((Idx * BitNum) + BitNum - 1);
      }
      assert((val >> (BitNum - 1)) == 0 && "value is too big");
      for (unsigned i = 0; i != BitNum - 1; ++i)
        Bits[(Idx * BitNum) + i] = val & (T(1) << i);
    } else {
      assert((val >> BitNum) == 0 && "value is too big");
      for (unsigned i = 0; i != BitNum; ++i)
        Bits[(Idx * BitNum) + i] = val & (T(1) << i);
    }
  }

public:
  /// Proxy that reads and writes a single packed element by index.
  class reference {
    PackedVector &Vec;
    const unsigned Idx;

  public:
    /// Default construction is deleted; a vector and index are required.
    reference() = delete;
    /// Bind this proxy to element \p idx of \p vec.
    ///
    /// \param vec Packed vector that owns the element.
    /// \param idx Index of the element to access.
    reference(PackedVector &vec, unsigned idx) : Vec(vec), Idx(idx) {}

    /// Store \p val into the referenced packed element.
    ///
    /// \param val Value that fits in \c BitNum bits.
    reference &operator=(T val) {
      Vec.setValue(Vec.Bits, Idx, val);
      return *this;
    }

    /// Read the referenced packed element as type \c T.
    operator T() const { return Vec.getValue(Vec.Bits, Idx); }
  };

  /// Construct an empty packed vector.
  PackedVector() = default;
  /// Construct a packed vector with \p size zero-initialized elements.
  ///
  /// \param size Number of packed elements to allocate.
  explicit PackedVector(unsigned size)
      : Bits(size * BitNum), NumElements(size) {}

  /// Return true if the vector contains no elements.
  bool empty() const { return NumElements == 0; }

  /// Return the number of packed elements.
  unsigned size() const { return NumElements; }

  /// Remove all elements and release bit storage.
  void clear() {
    Bits.clear();
    NumElements = 0;
  }

  /// Change the number of packed elements to \p N, growing or shrinking bits.
  ///
  /// \param N New element count.
  void resize(unsigned N) {
    Bits.resize(N * BitNum);
    NumElements = N;
  }

  /// Reserve bit capacity for at least \p N packed elements.
  ///
  /// \param N Number of elements to reserve for.
  void reserve(unsigned N) { Bits.reserve(N * BitNum); }

  /// Clear every packed bit without changing the element count.
  PackedVector &reset() {
    Bits.reset();
    return *this;
  }

  /// Append \p val as a new packed element.
  ///
  /// \param val Value that fits in \c BitNum bits.
  void push_back(T val) {
    resize(size() + 1);
    (*this)[size() - 1] = val;
  }

  /// Return a mutable proxy for the element at \p Idx.
  ///
  /// \param Idx Zero-based element index.
  reference operator[](unsigned Idx) { return reference(*this, Idx); }

  /// Return a copy of the packed element at \p Idx.
  ///
  /// \param Idx Zero-based element index.
  T operator[](unsigned Idx) const { return getValue(Bits, Idx); }

  /// Return true if both vectors have identical packed bit patterns.
  ///
  /// \param RHS Vector to compare against.
  bool operator==(const PackedVector &RHS) const { return Bits == RHS.Bits; }

  /// Return true if the packed bit patterns differ.
  ///
  /// \param RHS Vector to compare against.
  bool operator!=(const PackedVector &RHS) const { return Bits != RHS.Bits; }

  /// Bitwise-or each packed bit with the corresponding bit in \p RHS.
  ///
  /// \param RHS Vector whose bits are or-ed into this vector.
  PackedVector &operator|=(const PackedVector &RHS) {
    Bits |= RHS.Bits;
    return *this;
  }

  /// Access the underlying bit vector used for packed storage.
  const BitVectorTy &raw_bits() const { return Bits; }
  /// Access the underlying bit vector used for packed storage.
  BitVectorTy &raw_bits() { return Bits; }
};

} // end namespace llvm

#endif // LLVM_ADT_PACKEDVECTOR_H
