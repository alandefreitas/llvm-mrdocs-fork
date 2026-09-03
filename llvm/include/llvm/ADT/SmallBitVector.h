//===- llvm/ADT/SmallBitVector.h - 'Normally small' bit vectors -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the SmallBitVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLBITVECTOR_H
#define LLVM_ADT_SMALLBITVECTOR_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace llvm {

/// Variable-sized bit array optimized for small bit counts.
///
/// Uses one pointer-sized field as either an inline bit collection or a
/// pointer to a larger heap-allocated array, so small cases stay fast without
/// losing generality for large inputs.
class SmallBitVector {
  // TODO: In "large" mode, a pointer to a BitVector is used, leading to an
  // unnecessary level of indirection. It would be more efficient to use a
  // pointer to memory containing size, allocation size, and the array of bits.
  uintptr_t X = 1;

  enum {
    // The number of bits in this class.
    NumBaseBits = sizeof(uintptr_t) * CHAR_BIT,

    // One bit is used to discriminate between small and large mode. The
    // remaining bits are used for the small-mode representation.
    SmallNumRawBits = NumBaseBits - 1,

    // A few more bits are used to store the size of the bit set in small mode.
    // Theoretically this is a ceil-log2. These bits are encoded in the most
    // significant bits of the raw bits.
    SmallNumSizeBits = (NumBaseBits == 32 ? 5 :
                        NumBaseBits == 64 ? 6 :
                        SmallNumRawBits),

    // The remaining bits are used to store the actual set in small mode.
    SmallNumDataBits = SmallNumRawBits - SmallNumSizeBits
  };

  static_assert(NumBaseBits == 64 || NumBaseBits == 32,
                "Unsupported word size");

public:
  /// Type used for bit indices and sizes.
  using size_type = uintptr_t;

  /// Mutable proxy referring to a single bit in a SmallBitVector.
  class reference {
    SmallBitVector &TheVector;
    unsigned BitPos;

  public:
    /// Bind this reference to bit \p Idx of \p b.
    /// @param b Bit vector owning the bit.
    /// @param Idx Index of the referenced bit.
    reference(SmallBitVector &b, unsigned Idx) : TheVector(b), BitPos(Idx) {}

    /// Copy-construct; both references refer to the same bit.
    /// @param Other Bit reference to copy.
    reference(const reference &Other) = default;

    /// Assign from another bit reference by copying its boolean value.
    /// @param t Source bit reference.
    /// @return This reference.
    reference& operator=(reference t) {
      *this = bool(t);
      return *this;
    }

    /// Set or clear the referenced bit to match \p t.
    /// @param t Desired bit value.
    /// @return This reference.
    reference& operator=(bool t) {
      if (t)
        TheVector.set(BitPos);
      else
        TheVector.reset(BitPos);
      return *this;
    }

    /// Return the current value of the referenced bit.
    /// @return True if the referenced bit is set.
    operator bool() const {
      return const_cast<const SmallBitVector &>(TheVector).operator[](BitPos);
    }
  };

private:
  BitVector *getPointer() const {
    assert(!isSmall());
    return reinterpret_cast<BitVector *>(X);
  }

  void switchToSmall(uintptr_t NewSmallBits, size_type NewSize) {
    X = 1;
    setSmallSize(NewSize);
    setSmallBits(NewSmallBits);
  }

  void switchToLarge(BitVector *BV) {
    X = reinterpret_cast<uintptr_t>(BV);
    assert(!isSmall() && "Tried to use an unaligned pointer");
  }

  // Return all the bits used for the "small" representation; this includes
  // bits for the size as well as the element bits.
  uintptr_t getSmallRawBits() const {
    assert(isSmall());
    return X >> 1;
  }

  void setSmallRawBits(uintptr_t NewRawBits) {
    assert(isSmall());
    X = (NewRawBits << 1) | uintptr_t(1);
  }

  // Return the size.
  size_type getSmallSize() const {
    return getSmallRawBits() >> SmallNumDataBits;
  }

  void setSmallSize(size_type Size) {
    setSmallRawBits(getSmallBits() | (Size << SmallNumDataBits));
  }

  // Return the element bits.
  uintptr_t getSmallBits() const {
    return getSmallRawBits() & ~(~uintptr_t(0) << getSmallSize());
  }

  void setSmallBits(uintptr_t NewBits) {
    setSmallRawBits((NewBits & ~(~uintptr_t(0) << getSmallSize())) |
                    (getSmallSize() << SmallNumDataBits));
  }

public:
  /// Creates an empty bitvector.
  SmallBitVector() = default;

  /// Creates a bitvector of specified number of bits. All bits are initialized
  /// to the specified value.
  /// @param s Number of bits in the new vector.
  /// @param t Initial value for every bit.
  explicit SmallBitVector(unsigned s, bool t = false) {
    if (s <= SmallNumDataBits)
      switchToSmall(t ? ~uintptr_t(0) : 0, s);
    else
      switchToLarge(new BitVector(s, t));
  }

  /// SmallBitVector copy ctor.
  /// @param RHS Bit vector to copy.
  SmallBitVector(const SmallBitVector &RHS) {
    if (RHS.isSmall())
      X = RHS.X;
    else
      switchToLarge(new BitVector(*RHS.getPointer()));
  }

  /// Move-construct by taking ownership of \p RHS's storage.
  /// @param RHS Vector to move from; left empty in small mode.
  SmallBitVector(SmallBitVector &&RHS) : X(RHS.X) {
    RHS.X = 1;
  }

  /// Destroy large-mode storage if this vector spilled to a BitVector.
  ~SmallBitVector() {
    if (!isSmall())
      delete getPointer();
  }

  /// Iterator over indices of set bits.
  using const_set_bits_iterator = const_set_bits_iterator_impl<SmallBitVector>;
  /// Alias for \c const_set_bits_iterator.
  using set_iterator = const_set_bits_iterator;

  /// Iterator to the first set bit, or end if none are set.
  /// @return Iterator positioned at the first set bit, or end if none.
  const_set_bits_iterator set_bits_begin() const {
    return const_set_bits_iterator(*this);
  }

  /// Past-the-end iterator for the set-bits range.
  /// @return Past-the-end iterator for set-bit iteration.
  const_set_bits_iterator set_bits_end() const {
    return const_set_bits_iterator(*this, -1);
  }

  /// Return a range over the indices of all set bits.
  /// @return Iterator range covering indices of all set bits.
  iterator_range<const_set_bits_iterator> set_bits() const {
    return make_range(set_bits_begin(), set_bits_end());
  }

  /// Return true if this vector uses inline storage rather than a heap \c BitVector.
  /// @return True if bits are stored inline rather than on the heap.
  bool isSmall() const { return X & uintptr_t(1); }

  /// Tests whether there are no bits in this bitvector.
  /// @return True if the bit vector has size zero.
  bool empty() const {
    return isSmall() ? getSmallSize() == 0 : getPointer()->empty();
  }

  /// Returns the number of bits in this bitvector.
  /// @return Number of bits in this bit vector.
  size_type size() const {
    return isSmall() ? getSmallSize() : getPointer()->size();
  }

  /// Returns the number of bits which are set.
  /// @return Count of bits that are set to one.
  size_type count() const {
    if (isSmall()) {
      uintptr_t Bits = getSmallBits();
      return llvm::popcount(Bits);
    }
    return getPointer()->count();
  }

  /// Returns true if any bit is set.
  /// @return True if at least one bit is set.
  bool any() const {
    if (isSmall())
      return getSmallBits() != 0;
    return getPointer()->any();
  }

  /// Returns true if all bits are set.
  /// @return True if every bit in the vector is set.
  bool all() const {
    if (isSmall())
      return getSmallBits() == (uintptr_t(1) << getSmallSize()) - 1;
    return getPointer()->all();
  }

  /// Returns true if none of the bits are set.
  /// @return True if no bits are set.
  bool none() const {
    if (isSmall())
      return getSmallBits() == 0;
    return getPointer()->none();
  }

  /// Returns the index of the first set bit, -1 if none of the bits are set.
  /// @return Index of the first set bit, or -1 if none are set.
  int find_first() const {
    if (isSmall()) {
      uintptr_t Bits = getSmallBits();
      if (Bits == 0)
        return -1;
      return llvm::countr_zero(Bits);
    }
    return getPointer()->find_first();
  }

  /// Returns the index of the last set bit, -1 if none of the bits are set.
  /// @return Index of the last set bit, or -1 if none are set.
  int find_last() const {
    if (isSmall()) {
      uintptr_t Bits = getSmallBits();
      if (Bits == 0)
        return -1;
      return NumBaseBits - llvm::countl_zero(Bits) - 1;
    }
    return getPointer()->find_last();
  }

  /// Returns the index of the first unset bit, -1 if all of the bits are set.
  /// @return Index of the first unset bit, or -1 if all bits are set.
  int find_first_unset() const {
    if (isSmall()) {
      if (count() == getSmallSize())
        return -1;

      uintptr_t Bits = getSmallBits();
      return llvm::countr_one(Bits);
    }
    return getPointer()->find_first_unset();
  }

  /// Returns the index of the last unset bit, -1 if all of the bits are set.
  /// @return Index of the last unset bit, or -1 if all bits are set.
  int find_last_unset() const {
    if (isSmall()) {
      if (count() == getSmallSize())
        return -1;

      uintptr_t Bits = getSmallBits();
      // Set unused bits.
      Bits |= ~uintptr_t(0) << getSmallSize();
      return NumBaseBits - llvm::countl_one(Bits) - 1;
    }
    return getPointer()->find_last_unset();
  }

  /// Returns the index of the next set bit following the "Prev" bit.
  /// Returns -1 if the next set bit is not found.
  /// @param Prev Bit index after which to search for the next set bit.
  /// @return Index of the next set bit after \p Prev, or -1 if none.
  int find_next(unsigned Prev) const {
    if (isSmall()) {
      uintptr_t Bits = getSmallBits();
      // Mask off previous bits.
      Bits &= ~uintptr_t(0) << (Prev + 1);
      if (Bits == 0 || Prev + 1 >= getSmallSize())
        return -1;
      return llvm::countr_zero(Bits);
    }
    return getPointer()->find_next(Prev);
  }

  /// Returns the index of the next unset bit following the "Prev" bit.
  /// Returns -1 if the next unset bit is not found.
  /// @param Prev Bit index after which to search for the next unset bit.
  /// @return Index of the next unset bit after \p Prev, or -1 if none.
  int find_next_unset(unsigned Prev) const {
    if (isSmall()) {
      uintptr_t Bits = getSmallBits();
      // Mask in previous bits.
      Bits |= (uintptr_t(1) << (Prev + 1)) - 1;
      // Mask in unused bits.
      Bits |= ~uintptr_t(0) << getSmallSize();

      if (Bits == ~uintptr_t(0) || Prev + 1 >= getSmallSize())
        return -1;
      return llvm::countr_one(Bits);
    }
    return getPointer()->find_next_unset(Prev);
  }

  /// find_prev - Returns the index of the first set bit that precedes the
  /// the bit at \p PriorTo.  Returns -1 if all previous bits are unset.
  /// @param PriorTo Bit index before which to search for a set bit.
  /// @return Index of the nearest set bit before \p PriorTo, or -1 if none.
  int find_prev(unsigned PriorTo) const {
    if (isSmall()) {
      if (PriorTo == 0)
        return -1;

      --PriorTo;
      uintptr_t Bits = getSmallBits();
      Bits &= maskTrailingOnes<uintptr_t>(PriorTo + 1);
      if (Bits == 0)
        return -1;

      return NumBaseBits - llvm::countl_zero(Bits) - 1;
    }
    return getPointer()->find_prev(PriorTo);
  }

  /// Clear all bits.
  void clear() {
    if (!isSmall())
      delete getPointer();
    switchToSmall(0, 0);
  }

  /// Grow or shrink the bitvector.
  /// @param N New size in bits.
  /// @param t Value used to initialize newly added bits when growing.
  void resize(unsigned N, bool t = false) {
    if (!isSmall()) {
      getPointer()->resize(N, t);
    } else if (SmallNumDataBits >= N) {
      uintptr_t NewBits = t ? ~uintptr_t(0) << getSmallSize() : 0;
      setSmallSize(N);
      setSmallBits(NewBits | getSmallBits());
    } else {
      BitVector *BV = new BitVector(N, t);
      uintptr_t OldBits = getSmallBits();
      for (size_type I = 0, E = getSmallSize(); I != E; ++I)
        (*BV)[I] = (OldBits >> I) & 1;
      switchToLarge(BV);
    }
  }

  /// Reserve space for at least \p N bits in the bitvector.
  /// @param N Minimum number of bits to reserve capacity for.
  void reserve(unsigned N) {
    if (isSmall()) {
      if (N > SmallNumDataBits) {
        uintptr_t OldBits = getSmallRawBits();
        size_type SmallSize = getSmallSize();
        BitVector *BV = new BitVector(SmallSize);
        for (size_type I = 0; I < SmallSize; ++I)
          if ((OldBits >> I) & 1)
            BV->set(I);
        BV->reserve(N);
        switchToLarge(BV);
      }
    } else {
      getPointer()->reserve(N);
    }
  }

  /// Set all bits in the bitvector.
  /// @return Reference to this bit vector.
  SmallBitVector &set() {
    if (isSmall())
      setSmallBits(~uintptr_t(0));
    else
      getPointer()->set();
    return *this;
  }

  /// Set bit \p Idx in the bitvector.
  /// @param Idx Index of the bit to set.
  /// @return Reference to this bit vector.
  SmallBitVector &set(unsigned Idx) {
    if (isSmall()) {
      assert(Idx <= static_cast<unsigned>(
                        std::numeric_limits<uintptr_t>::digits) &&
             "undefined behavior");
      setSmallBits(getSmallBits() | (uintptr_t(1) << Idx));
    }
    else
      getPointer()->set(Idx);
    return *this;
  }

  /// Efficiently set a range of bits in [I, E)
  /// @param I Inclusive start of the range to set.
  /// @param E Exclusive end of the range to set.
  /// @return Reference to this bit vector.
  SmallBitVector &set(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to set backwards range!");
    assert(E <= size() && "Attempted to set out-of-bounds range!");
    if (I == E) return *this;
    if (isSmall()) {
      uintptr_t EMask = ((uintptr_t)1) << E;
      uintptr_t IMask = ((uintptr_t)1) << I;
      uintptr_t Mask = EMask - IMask;
      setSmallBits(getSmallBits() | Mask);
    } else {
      getPointer()->set(I, E);
    }
    return *this;
  }

  /// Reset all bits in the bitvector.
  /// @return Reference to this bit vector.
  SmallBitVector &reset() {
    if (isSmall())
      setSmallBits(0);
    else
      getPointer()->reset();
    return *this;
  }

  /// Clear bit \p Idx and return this vector.
  /// @param Idx Bit index to clear.
  /// @return Reference to this bit vector.
  SmallBitVector &reset(unsigned Idx) {
    if (isSmall())
      setSmallBits(getSmallBits() & ~(uintptr_t(1) << Idx));
    else
      getPointer()->reset(Idx);
    return *this;
  }

  /// Efficiently reset a range of bits in [I, E)
  /// @param I Inclusive start of the range to clear.
  /// @param E Exclusive end of the range to clear.
  /// @return Reference to this bit vector.
  SmallBitVector &reset(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to reset backwards range!");
    assert(E <= size() && "Attempted to reset out-of-bounds range!");
    if (I == E) return *this;
    if (isSmall()) {
      uintptr_t EMask = ((uintptr_t)1) << E;
      uintptr_t IMask = ((uintptr_t)1) << I;
      uintptr_t Mask = EMask - IMask;
      setSmallBits(getSmallBits() & ~Mask);
    } else {
      getPointer()->reset(I, E);
    }
    return *this;
  }

  /// Flip all bits in the bitvector.
  /// @return Reference to this bit vector.
  SmallBitVector &flip() {
    if (isSmall())
      setSmallBits(~getSmallBits());
    else
      getPointer()->flip();
    return *this;
  }

  /// Toggle bit \p Idx and return this vector.
  /// @param Idx Bit index to flip.
  /// @return Reference to this bit vector.
  SmallBitVector &flip(unsigned Idx) {
    if (isSmall())
      setSmallBits(getSmallBits() ^ (uintptr_t(1) << Idx));
    else
      getPointer()->flip(Idx);
    return *this;
  }

  /// Return a copy of this vector with every bit inverted.
  /// @return New bit vector with all bits flipped.
  SmallBitVector operator~() const {
    return SmallBitVector(*this).flip();
  }

  /// Return a mutable reference to bit \p Idx.
  /// @param Idx Bit index to access.
  /// @return Proxy referring to bit \p Idx.
  reference operator[](unsigned Idx) {
    assert(Idx < size() && "Out-of-bounds Bit access.");
    return reference(*this, Idx);
  }

  /// Return the value of bit \p Idx.
  /// @param Idx Bit index to read.
  /// @return True if bit \p Idx is set.
  bool operator[](unsigned Idx) const {
    assert(Idx < size() && "Out-of-bounds Bit access.");
    if (isSmall())
      return ((getSmallBits() >> Idx) & 1) != 0;
    return getPointer()->operator[](Idx);
  }

  /// Return the last element in the vector.
  /// @return Value of the last bit in the vector.
  bool back() const {
    assert(!empty() && "Getting last element of empty vector.");
    return (*this)[size() - 1];
  }

  /// Returns true if bit \p Idx is set.
  /// @param Idx Bit index to test.
  /// @return True if bit \p Idx is set.
  bool test(unsigned Idx) const {
    return (*this)[Idx];
  }

  /// Returns true if all bits in the range [Begin, End) are set.
  /// @param Begin Inclusive start of the range.
  /// @param End Exclusive end of the range.
  /// @return True if every bit in [Begin, End) is set.
  bool test_all(unsigned Begin, unsigned End) const {
    for (unsigned i = Begin; i < End; ++i) {
      if (!test(i))
        return false;
    }
    return true;
  }

  /// Returns true if any of the bits in the range [Begin, End) are set.
  /// @param Begin Inclusive start of the range.
  /// @param End Exclusive end of the range.
  /// @return True if any bit in [Begin, End) is set.
  bool test_any(unsigned Begin, unsigned End) const {
    for (unsigned i = Begin; i < End; ++i) {
      if (test(i))
        return true;
    }
    return false;
  }

  /// Append a single bit to the end of the vector.
  /// @param Val Value of the bit to append.
  void push_back(bool Val) {
    resize(size() + 1, Val);
  }

  /// Pop one bit from the end of the vector.
  void pop_back() {
    assert(!empty() && "Empty vector has no element to pop.");
    resize(size() - 1);
  }

  /// Test if any common bits are set.
  /// @param RHS Bit vector to compare against for overlapping set bits.
  /// @return True if any bit is set in both this vector and \p RHS.
  bool anyCommon(const SmallBitVector &RHS) const {
    if (isSmall() && RHS.isSmall())
      return (getSmallBits() & RHS.getSmallBits()) != 0;
    if (!isSmall() && !RHS.isSmall())
      return getPointer()->anyCommon(*RHS.getPointer());

    for (unsigned i = 0, e = std::min(size(), RHS.size()); i != e; ++i)
      if (test(i) && RHS.test(i))
        return true;
    return false;
  }

  /// Return true if this and \p RHS have the same size and bit pattern.
  /// @param RHS Bit vector to compare against.
  /// @return True if the vectors have the same size and bit pattern.
  bool operator==(const SmallBitVector &RHS) const {
    if (size() != RHS.size())
      return false;
    if (isSmall() && RHS.isSmall())
      return getSmallBits() == RHS.getSmallBits();
    else if (!isSmall() && !RHS.isSmall())
      return *getPointer() == *RHS.getPointer();
    else {
      for (size_type I = 0, E = size(); I != E; ++I) {
        if ((*this)[I] != RHS[I])
          return false;
      }
      return true;
    }
  }

  /// Return true if this and \p RHS differ in size or any bit.
  /// @param RHS Bit vector to compare against.
  /// @return True if the vectors differ in size or bit pattern.
  bool operator!=(const SmallBitVector &RHS) const {
    return !(*this == RHS);
  }

  /// Intersect this vector with \p RHS in place, resizing to the larger size.
  /// @param RHS Bit vector to AND with this one.
  /// @return Reference to this bit vector.
  // FIXME BitVector::operator&= does not resize the LHS but this does
  SmallBitVector &operator&=(const SmallBitVector &RHS) {
    resize(std::max(size(), RHS.size()));
    if (isSmall() && RHS.isSmall())
      setSmallBits(getSmallBits() & RHS.getSmallBits());
    else if (!isSmall() && !RHS.isSmall())
      getPointer()->operator&=(*RHS.getPointer());
    else {
      size_type I, E;
      for (I = 0, E = std::min(size(), RHS.size()); I != E; ++I)
        (*this)[I] = test(I) && RHS.test(I);
      for (E = size(); I != E; ++I)
        reset(I);
    }
    return *this;
  }

  /// Reset bits that are set in RHS. Same as *this &= ~RHS.
  /// @param RHS Bit vector whose set bits are cleared in this vector.
  /// @return Reference to this bit vector.
  SmallBitVector &reset(const SmallBitVector &RHS) {
    if (isSmall() && RHS.isSmall())
      setSmallBits(getSmallBits() & ~RHS.getSmallBits());
    else if (!isSmall() && !RHS.isSmall())
      getPointer()->reset(*RHS.getPointer());
    else
      for (unsigned i = 0, e = std::min(size(), RHS.size()); i != e; ++i)
        if (RHS.test(i))
          reset(i);

    return *this;
  }

  /// Check if (This - RHS) is non-zero.
  /// This is the same as reset(RHS) and any().
  /// @param RHS Bit vector subtracted from this one before testing for any set bits.
  /// @return True if any bit is set in this vector but not in \p RHS.
  bool test(const SmallBitVector &RHS) const {
    if (isSmall() && RHS.isSmall())
      return (getSmallBits() & ~RHS.getSmallBits()) != 0;
    if (!isSmall() && !RHS.isSmall())
      return getPointer()->test(*RHS.getPointer());

    unsigned i, e;
    for (i = 0, e = std::min(size(), RHS.size()); i != e; ++i)
      if (test(i) && !RHS.test(i))
        return true;

    for (e = size(); i != e; ++i)
      if (test(i))
        return true;

    return false;
  }

  /// Check if This is a subset of RHS.
  /// @param RHS Bit vector that should contain every set bit of this vector.
  /// @return True if every set bit of this vector is also set in \p RHS.
  bool subsetOf(const SmallBitVector &RHS) const { return !test(RHS); }

  /// OR this vector with \p RHS in place, resizing if needed.
  /// @param RHS Bit vector to OR with.
  /// @return Reference to this bit vector.
  SmallBitVector &operator|=(const SmallBitVector &RHS) {
    resize(std::max(size(), RHS.size()));
    if (isSmall() && RHS.isSmall())
      setSmallBits(getSmallBits() | RHS.getSmallBits());
    else if (!isSmall() && !RHS.isSmall())
      getPointer()->operator|=(*RHS.getPointer());
    else {
      for (size_type I = 0, E = RHS.size(); I != E; ++I)
        (*this)[I] = test(I) || RHS.test(I);
    }
    return *this;
  }

  /// XOR this vector with \p RHS in place, resizing if needed.
  /// @param RHS Bit vector to XOR with.
  /// @return Reference to this bit vector.
  SmallBitVector &operator^=(const SmallBitVector &RHS) {
    resize(std::max(size(), RHS.size()));
    if (isSmall() && RHS.isSmall())
      setSmallBits(getSmallBits() ^ RHS.getSmallBits());
    else if (!isSmall() && !RHS.isSmall())
      getPointer()->operator^=(*RHS.getPointer());
    else {
      for (size_type I = 0, E = RHS.size(); I != E; ++I)
        (*this)[I] = test(I) != RHS.test(I);
    }
    return *this;
  }

  /// Shift bits left by \p N positions; vacated low bits become zero.
  /// @param N Number of bit positions to shift.
  /// @return Reference to this bit vector.
  SmallBitVector &operator<<=(unsigned N) {
    if (isSmall())
      setSmallBits(getSmallBits() << N);
    else
      getPointer()->operator<<=(N);
    return *this;
  }

  /// Shift bits right by \p N positions; vacated high bits become zero.
  /// @param N Number of bit positions to shift.
  /// @return Reference to this bit vector.
  SmallBitVector &operator>>=(unsigned N) {
    if (isSmall())
      setSmallBits(getSmallBits() >> N);
    else
      getPointer()->operator>>=(N);
    return *this;
  }

  /// Copy-assign from \p RHS, switching between small and large storage as needed.
  /// @param RHS Bit vector to copy from.
  /// @return Reference to this bit vector.
  const SmallBitVector &operator=(const SmallBitVector &RHS) {
    if (isSmall()) {
      if (RHS.isSmall())
        X = RHS.X;
      else
        switchToLarge(new BitVector(*RHS.getPointer()));
    } else {
      if (!RHS.isSmall())
        *getPointer() = *RHS.getPointer();
      else {
        delete getPointer();
        X = RHS.X;
      }
    }
    return *this;
  }

  /// Move-assign from \p RHS, taking its storage and leaving \p RHS empty.
  /// @param RHS Bit vector to move from.
  /// @return Reference to this bit vector.
  const SmallBitVector &operator=(SmallBitVector &&RHS) {
    if (this != &RHS) {
      clear();
      swap(RHS);
    }
    return *this;
  }

  /// Exchange contents with \p RHS.
  /// @param RHS Other bit vector to swap with.
  void swap(SmallBitVector &RHS) {
    std::swap(X, RHS.X);
  }

  /// Add '1' bits from Mask to this vector. Don't resize.
  /// This computes "*this |= Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void setBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    if (isSmall())
      applyMask<true, false>(Mask, MaskWords);
    else
      getPointer()->setBitsInMask(Mask, MaskWords);
  }

  /// Clear any bits in this vector that are set in Mask. Don't resize.
  /// This computes "*this &= ~Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void clearBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    if (isSmall())
      applyMask<false, false>(Mask, MaskWords);
    else
      getPointer()->clearBitsInMask(Mask, MaskWords);
  }

  /// Add a bit to this vector for every '0' bit in Mask. Don't resize.
  /// This computes "*this |= ~Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void setBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    if (isSmall())
      applyMask<true, true>(Mask, MaskWords);
    else
      getPointer()->setBitsNotInMask(Mask, MaskWords);
  }

  /// Clear a bit in this vector for every '0' bit in Mask. Don't resize.
  /// This computes "*this &= Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void clearBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    if (isSmall())
      applyMask<false, true>(Mask, MaskWords);
    else
      getPointer()->clearBitsNotInMask(Mask, MaskWords);
  }

  /// Return the underlying word storage; in small mode, write bits into \p Store.
  /// @param Store Scratch word used when bits are stored inline.
  /// @return ArrayRef over the bit words (one word in small mode).
  ArrayRef<uintptr_t> getData(uintptr_t &Store) const {
    if (!isSmall())
      return getPointer()->getData();
    Store = getSmallBits();
    return Store;
  }

private:
  template <bool AddBits, bool InvertMask>
  void applyMask(const uint32_t *Mask, unsigned MaskWords) {
    assert(MaskWords <= sizeof(uintptr_t) && "Mask is larger than base!");
    uintptr_t M = Mask[0];
    if (NumBaseBits == 64)
      M |= uint64_t(Mask[1]) << 32;
    if (InvertMask)
      M = ~M;
    if (AddBits)
      setSmallBits(getSmallBits() | M);
    else
      setSmallBits(getSmallBits() & ~M);
  }
};

/// Return the bitwise AND of \p LHS and \p RHS.
/// @param LHS Left-hand bit vector.
/// @param RHS Right-hand bit vector.
/// @return New bit vector that is the bitwise AND of \p LHS and \p RHS.
inline SmallBitVector
operator&(const SmallBitVector &LHS, const SmallBitVector &RHS) {
  SmallBitVector Result(LHS);
  Result &= RHS;
  return Result;
}

/// Return the bitwise OR of \p LHS and \p RHS.
/// @param LHS Left-hand bit vector.
/// @param RHS Right-hand bit vector.
/// @return New bit vector that is the bitwise OR of \p LHS and \p RHS.
inline SmallBitVector
operator|(const SmallBitVector &LHS, const SmallBitVector &RHS) {
  SmallBitVector Result(LHS);
  Result |= RHS;
  return Result;
}

/// Return the bitwise XOR of \p LHS and \p RHS.
/// @param LHS Left-hand bit vector.
/// @param RHS Right-hand bit vector.
/// @return New bit vector with bits set where \p LHS and \p RHS differ.
inline SmallBitVector
operator^(const SmallBitVector &LHS, const SmallBitVector &RHS) {
  SmallBitVector Result(LHS);
  Result ^= RHS;
  return Result;
}

/// Provide DenseMapInfo for SmallBitVector, hashing size and word storage.
template <> struct DenseMapInfo<SmallBitVector> {
  /// Compute a hash code for bit vector \p V.
  /// @param V Bit vector to hash.
  /// @return Hash value for \p V.
  static unsigned getHashValue(const SmallBitVector &V) {
    uintptr_t Store;
    return DenseMapInfo<
        std::pair<SmallBitVector::size_type, ArrayRef<uintptr_t>>>::
        getHashValue(std::make_pair(V.size(), V.getData(Store)));
  }
  /// Return true if \p LHS and \p RHS have equal size and bit pattern.
  /// @param LHS First bit vector.
  /// @param RHS Second bit vector.
  /// @return True if the vectors are equal.
  static bool isEqual(const SmallBitVector &LHS, const SmallBitVector &RHS) {
    return LHS == RHS;
  }
};
} // end namespace llvm

namespace std {

/// Implement std::swap in terms of BitVector swap.
inline void
swap(llvm::SmallBitVector &LHS, llvm::SmallBitVector &RHS) {
  LHS.swap(RHS);
}

} // end namespace std

#endif // LLVM_ADT_SMALLBITVECTOR_H
