//===- llvm/ADT/BitVector.h - Bit vectors -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the BitVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BITVECTOR_H
#define LLVM_ADT_BITVECTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

namespace llvm {

/// ForwardIterator for the bits that are set.
/// Iterators get invalidated when resize / reserve is called.
template <typename BitVectorT> class const_set_bits_iterator_impl {
  const BitVectorT &Parent;
  int Current = 0;

  void advance() {
    assert(Current != -1 && "Trying to advance past end.");
    Current = Parent.find_next(Current);
  }

  void retreat() {
    if (Current == -1) {
      Current = Parent.find_last();
    } else {
      Current = Parent.find_prev(Current);
    }
  }

public:
  /// Bidirectional iterator category for set-bit iteration.
  using iterator_category = std::bidirectional_iterator_tag;
  /// Distance between two set-bit iterators.
  using difference_type = std::ptrdiff_t;
  /// Index of a set bit in the parent bit vector.
  using value_type = unsigned;
  /// Pointer to a set-bit index (unused; iterator is not contiguous).
  using pointer = const value_type *;
  /// Reference to a set-bit index.
  using reference = value_type;

  /// Construct an iterator positioned at bit index \p Current in \p Parent.
  /// @param Parent Bit vector being iterated.
  /// @param Current Index of the current set bit, or -1 for end.
  const_set_bits_iterator_impl(const BitVectorT &Parent, int Current)
      : Parent(Parent), Current(Current) {}
  /// Construct an iterator at the first set bit of \p Parent, or end if none.
  /// @param Parent Bit vector being iterated.
  explicit const_set_bits_iterator_impl(const BitVectorT &Parent)
      : const_set_bits_iterator_impl(Parent, Parent.find_first()) {}
  /// Copy-construct an iterator at the same set-bit position.
  /// @param Other Iterator to copy.
  const_set_bits_iterator_impl(const const_set_bits_iterator_impl &Other) =
      default;

  /// Advance to the next set bit and return the prior iterator position.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before advancing.
  const_set_bits_iterator_impl operator++(int Unused) {
    auto Prev = *this;
    advance();
    return Prev;
  }

  /// Move to the next set bit and return this iterator.
  /// @return Reference to this iterator after advancing.
  const_set_bits_iterator_impl &operator++() {
    advance();
    return *this;
  }

  /// Retreat to the previous set bit and return the prior iterator position.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before retreating.
  const_set_bits_iterator_impl operator--(int Unused) {
    auto Prev = *this;
    retreat();
    return Prev;
  }

  /// Move to the previous set bit and return this iterator.
  /// @return Reference to this iterator after retreating.
  const_set_bits_iterator_impl &operator--() {
    retreat();
    return *this;
  }

  /// Return the index of the current set bit.
  /// @return Index of the current set bit in the parent bit vector.
  unsigned operator*() const { return Current; }

  /// Return true if both iterators point at the same set-bit index.
  /// @param Other Iterator from the same bit vector.
  /// @return True if both iterators have the same Current index.
  bool operator==(const const_set_bits_iterator_impl &Other) const {
    assert(&Parent == &Other.Parent &&
           "Comparing iterators from different BitVectors");
    return Current == Other.Current;
  }

  /// Return true if the iterators point at different set-bit indices.
  /// @param Other Iterator from the same bit vector.
  /// @return True if the iterators differ in Current index.
  bool operator!=(const const_set_bits_iterator_impl &Other) const {
    assert(&Parent == &Other.Parent &&
           "Comparing iterators from different BitVectors");
    return Current != Other.Current;
  }
};

/// Dynamically sized bit vector with efficient bit-level operations.
class BitVector {
  using BitWord = uintptr_t;

  enum { BITWORD_SIZE = (unsigned)sizeof(BitWord) * CHAR_BIT };

  static_assert(BITWORD_SIZE == 64 || BITWORD_SIZE == 32,
                "Unsupported word size");

  using Storage = SmallVector<BitWord>;

  Storage Bits;  // Actual bits.
  unsigned Size = 0; // Size of bitvector in bits.

public:
  /// Unsigned type used for bit indices and bit-vector sizes.
  using size_type = unsigned;

  /// Mutable proxy referring to a single bit in a BitVector.
  class reference {

    BitWord *WordRef;
    unsigned BitPos;

  public:
    /// Bind this proxy to bit \p Idx of \p b.
    /// @param b Bit vector that owns the bit.
    /// @param Idx Bit index within \p b.
    reference(BitVector &b, unsigned Idx) {
      WordRef = &b.Bits[Idx / BITWORD_SIZE];
      BitPos = Idx % BITWORD_SIZE;
    }

    /// Default construction is deleted; a bit reference must bind to a vector.
    reference() = delete;
    /// Copy-construct a proxy referring to the same bit.
    /// @param Other Bit reference to copy.
    reference(const reference &Other) = default;

    /// Copy the bit value from another reference into this bit.
    /// @param t Source bit reference.
    /// @return Reference to this bit proxy.
    reference &operator=(reference t) {
      *this = bool(t);
      return *this;
    }

    /// Set this bit to \p t.
    /// @param t New bit value.
    /// @return Reference to this bit proxy.
    reference& operator=(bool t) {
      if (t)
        *WordRef |= BitWord(1) << BitPos;
      else
        *WordRef &= ~(BitWord(1) << BitPos);
      return *this;
    }

    /// Return the bit value as a bool.
    /// @return True if the referenced bit is set.
    operator bool() const {
      return ((*WordRef) & (BitWord(1) << BitPos)) != 0;
    }
  };

  /// Const iterator over indices of set bits.
  using const_set_bits_iterator = const_set_bits_iterator_impl<BitVector>;
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
  /// Range over the indices of all bits that are set.
  /// @return Iterator range covering indices of all set bits.
  iterator_range<const_set_bits_iterator> set_bits() const {
    return make_range(set_bits_begin(), set_bits_end());
  }

  /// BitVector default ctor - Creates an empty bitvector.
  BitVector() = default;

  /// BitVector ctor - Creates a bitvector of specified number of bits. All
  /// bits are initialized to the specified value.
  /// @param s Number of bits in the new vector.
  /// @param t Initial value for every bit.
  explicit BitVector(unsigned s, bool t = false)
      : Bits(NumBitWords(s), 0 - (BitWord)t), Size(s) {
    if (t)
      clear_unused_bits();
  }

  /// Returns whether there are no bits in this bitvector.
  /// @return True if the bit vector has size zero.
  bool empty() const { return Size == 0; }

  /// Returns the number of bits in this bitvector.
  /// @return Number of bits in this bit vector.
  size_type size() const { return Size; }

  /// Returns the number of bits which are set.
  /// @return Count of bits that are set to one.
  size_type count() const {
    unsigned NumBits = 0;
    for (auto Bit : Bits)
      NumBits += llvm::popcount(Bit);
    return NumBits;
  }

  /// Returns true if any bit is set.
  /// @return True if at least one bit is set.
  bool any() const {
    return any_of(Bits, [](BitWord Bit) { return Bit != 0; });
  }

  /// Returns true if all bits are set.
  /// @return True if every bit in the vector is set.
  bool all() const {
    for (unsigned i = 0; i < Size / BITWORD_SIZE; ++i)
      if (Bits[i] != ~BitWord(0))
        return false;

    // If bits remain check that they are ones. The unused bits are always zero.
    if (unsigned Remainder = Size % BITWORD_SIZE)
      return Bits[Size / BITWORD_SIZE] == (BitWord(1) << Remainder) - 1;

    return true;
  }

  /// Returns true if none of the bits are set.
  /// @return True if no bits are set.
  bool none() const {
    return !any();
  }

  /// Returns the index of the first set/unset bit, depending on \p Set, in
  /// the range [Begin, End). Returns -1 if all bits in the range are unset/set.
  /// @param Begin Inclusive start of the search range.
  /// @param End Exclusive end of the search range.
  /// @param Set If true, find the first set bit; otherwise the first unset bit.
  /// @return Index of the first matching bit in [Begin, End), or -1 if none.
  int find_first_in(unsigned Begin, unsigned End, bool Set = true) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned FirstWord = Begin / BITWORD_SIZE;
    unsigned LastWord = (End - 1) / BITWORD_SIZE;

    // Check subsequent words.
    // The code below is based on search for the first _set_ bit. If
    // we're searching for the first _unset_, we just take the
    // complement of each word before we use it and apply
    // the same method.
    for (unsigned i = FirstWord; i <= LastWord; ++i) {
      BitWord Copy = Bits[i];
      if (!Set)
        Copy = ~Copy;

      if (i == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy &= maskTrailingZeros<BitWord>(FirstBit);
      }

      if (i == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy &= maskTrailingOnes<BitWord>(LastBit + 1);
      }
      if (Copy != 0)
        return i * BITWORD_SIZE + llvm::countr_zero(Copy);
    }
    return -1;
  }

  /// Returns the index of the last set bit in the range [Begin, End).
  /// Returns -1 if all bits in the range are unset.
  /// @param Begin Inclusive start of the search range.
  /// @param End Exclusive end of the search range.
  /// @return Index of the last set bit in [Begin, End), or -1 if none.
  int find_last_in(unsigned Begin, unsigned End) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned LastWord = (End - 1) / BITWORD_SIZE;
    unsigned FirstWord = Begin / BITWORD_SIZE;

    for (unsigned i = LastWord + 1; i >= FirstWord + 1; --i) {
      unsigned CurrentWord = i - 1;

      BitWord Copy = Bits[CurrentWord];
      if (CurrentWord == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy &= maskTrailingOnes<BitWord>(LastBit + 1);
      }

      if (CurrentWord == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy &= maskTrailingZeros<BitWord>(FirstBit);
      }

      if (Copy != 0)
        return (CurrentWord + 1) * BITWORD_SIZE - llvm::countl_zero(Copy) - 1;
    }

    return -1;
  }

  /// Returns the index of the first unset bit in the range [Begin, End).
  /// Returns -1 if all bits in the range are set.
  /// @param Begin Inclusive start of the search range.
  /// @param End Exclusive end of the search range.
  /// @return Index of the first unset bit in [Begin, End), or -1 if none.
  int find_first_unset_in(unsigned Begin, unsigned End) const {
    return find_first_in(Begin, End, /* Set = */ false);
  }

  /// Returns the index of the last unset bit in the range [Begin, End).
  /// Returns -1 if all bits in the range are set.
  /// @param Begin Inclusive start of the search range.
  /// @param End Exclusive end of the search range.
  /// @return Index of the last unset bit in [Begin, End), or -1 if none.
  int find_last_unset_in(unsigned Begin, unsigned End) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned LastWord = (End - 1) / BITWORD_SIZE;
    unsigned FirstWord = Begin / BITWORD_SIZE;

    for (unsigned i = LastWord + 1; i >= FirstWord + 1; --i) {
      unsigned CurrentWord = i - 1;

      BitWord Copy = Bits[CurrentWord];
      if (CurrentWord == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy |= maskTrailingZeros<BitWord>(LastBit + 1);
      }

      if (CurrentWord == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy |= maskTrailingOnes<BitWord>(FirstBit);
      }

      if (Copy != ~BitWord(0)) {
        unsigned Result =
            (CurrentWord + 1) * BITWORD_SIZE - llvm::countl_one(Copy) - 1;
        return Result < Size ? Result : -1;
      }
    }
    return -1;
  }

  /// Returns the index of the first set bit, -1 if none of the bits are set.
  /// @return Index of the first set bit, or -1 if none are set.
  int find_first() const { return find_first_in(0, Size); }

  /// Returns the index of the last set bit, -1 if none of the bits are set.
  /// @return Index of the last set bit, or -1 if none are set.
  int find_last() const { return find_last_in(0, Size); }

  /// Returns the index of the next set bit following the "Prev" bit.
  /// Returns -1 if the next set bit is not found.
  /// @param Prev Bit index after which to search for the next set bit.
  /// @return Index of the next set bit after \p Prev, or -1 if none.
  int find_next(unsigned Prev) const { return find_first_in(Prev + 1, Size); }

  /// Returns the index of the first set bit that precedes the bit at
  /// \p PriorTo. Returns -1 if all previous bits are unset.
  /// @param PriorTo Bit index before which to search for a set bit.
  /// @return Index of the nearest set bit before \p PriorTo, or -1 if none.
  int find_prev(unsigned PriorTo) const { return find_last_in(0, PriorTo); }

  /// Returns the index of the first unset bit, -1 if all of the bits are set.
  /// @return Index of the first unset bit, or -1 if all bits are set.
  int find_first_unset() const { return find_first_unset_in(0, Size); }

  /// Returns the index of the next unset bit following the \p Prev bit.
  /// Returns -1 if all remaining bits are set.
  /// @param Prev Bit index after which to search for the next unset bit.
  /// @return Index of the next unset bit after \p Prev, or -1 if none.
  int find_next_unset(unsigned Prev) const {
    return find_first_unset_in(Prev + 1, Size);
  }

  /// Returns the index of the last unset bit, -1 if all of the bits are set.
  /// @return Index of the last unset bit, or -1 if all bits are set.
  int find_last_unset() const { return find_last_unset_in(0, Size); }

  /// Returns the index of the first unset bit that precedes the bit at
  /// \p PriorTo. Returns -1 if all previous bits are set.
  /// @param PriorTo Bit index before which to search for an unset bit.
  /// @return Index of the nearest unset bit before \p PriorTo, or -1 if none.
  int find_prev_unset(unsigned PriorTo) const {
    return find_last_unset_in(0, PriorTo);
  }

  /// Removes all bits from the bitvector.
  void clear() {
    Size = 0;
    Bits.clear();
  }

  /// Grow or shrink the bitvector.
  /// @param N New size in bits.
  /// @param t Value used to initialize newly added bits when growing.
  void resize(unsigned N, bool t = false) {
    set_unused_bits(t);
    Size = N;
    Bits.resize(NumBitWords(N), 0 - BitWord(t));
    clear_unused_bits();
  }

  /// Reserve space for atleast \p N bits in the bitvector.
  /// @param N Minimum number of bits to reserve capacity for.
  void reserve(unsigned N) { Bits.reserve(NumBitWords(N)); }

  /// Set all bits in the bitvector.
  /// @return Reference to this bit vector.
  BitVector &set() {
    init_words(true);
    clear_unused_bits();
    return *this;
  }

  /// Set bit \p Idx in the bitvector.
  /// @param Idx Bit index to set.
  /// @return Reference to this bit vector.
  BitVector &set(unsigned Idx) {
    assert(Idx < Size && "access in bound");
    Bits[Idx / BITWORD_SIZE] |= BitWord(1) << (Idx % BITWORD_SIZE);
    return *this;
  }

  /// Efficiently set a range of bits in [I, E)
  /// @param I Inclusive start of the range to set.
  /// @param E Exclusive end of the range to set.
  /// @return Reference to this bit vector.
  BitVector &set(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to set backwards range!");
    assert(E <= size() && "Attempted to set out-of-bounds range!");

    if (I == E) return *this;

    if (I / BITWORD_SIZE == E / BITWORD_SIZE) {
      BitWord EMask = BitWord(1) << (E % BITWORD_SIZE);
      BitWord IMask = BitWord(1) << (I % BITWORD_SIZE);
      BitWord Mask = EMask - IMask;
      Bits[I / BITWORD_SIZE] |= Mask;
      return *this;
    }

    BitWord PrefixMask = ~BitWord(0) << (I % BITWORD_SIZE);
    Bits[I / BITWORD_SIZE] |= PrefixMask;
    I = alignTo(I, BITWORD_SIZE);

    for (; I + BITWORD_SIZE <= E; I += BITWORD_SIZE)
      Bits[I / BITWORD_SIZE] = ~BitWord(0);

    BitWord PostfixMask = (BitWord(1) << (E % BITWORD_SIZE)) - 1;
    if (I < E)
      Bits[I / BITWORD_SIZE] |= PostfixMask;

    return *this;
  }

  /// Reset all bits in the bitvector.
  /// @return Reference to this bit vector.
  BitVector &reset() {
    init_words(false);
    return *this;
  }

  /// Reset bit \p Idx in the bitvector.
  /// @param Idx Bit index to clear.
  /// @return Reference to this bit vector.
  BitVector &reset(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] &= ~(BitWord(1) << (Idx % BITWORD_SIZE));
    return *this;
  }

  /// Efficiently reset a range of bits in [I, E)
  /// @param I Inclusive start of the range to clear.
  /// @param E Exclusive end of the range to clear.
  /// @return Reference to this bit vector.
  BitVector &reset(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to reset backwards range!");
    assert(E <= size() && "Attempted to reset out-of-bounds range!");

    if (I == E) return *this;

    if (I / BITWORD_SIZE == E / BITWORD_SIZE) {
      BitWord EMask = BitWord(1) << (E % BITWORD_SIZE);
      BitWord IMask = BitWord(1) << (I % BITWORD_SIZE);
      BitWord Mask = EMask - IMask;
      Bits[I / BITWORD_SIZE] &= ~Mask;
      return *this;
    }

    BitWord PrefixMask = ~BitWord(0) << (I % BITWORD_SIZE);
    Bits[I / BITWORD_SIZE] &= ~PrefixMask;
    I = alignTo(I, BITWORD_SIZE);

    for (; I + BITWORD_SIZE <= E; I += BITWORD_SIZE)
      Bits[I / BITWORD_SIZE] = BitWord(0);

    BitWord PostfixMask = (BitWord(1) << (E % BITWORD_SIZE)) - 1;
    if (I < E)
      Bits[I / BITWORD_SIZE] &= ~PostfixMask;

    return *this;
  }

  /// Flip all bits in the bitvector.
  /// @return Reference to this bit vector.
  BitVector &flip() {
    for (auto &Bit : Bits)
      Bit = ~Bit;
    clear_unused_bits();
    return *this;
  }

  /// Flip bit \p Idx in the bitvector.
  /// @param Idx Bit index to toggle.
  /// @return Reference to this bit vector.
  BitVector &flip(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] ^= BitWord(1) << (Idx % BITWORD_SIZE);
    return *this;
  }

  /// Return a mutable proxy for bit \p Idx.
  /// @param Idx Bit index.
  /// @return Mutable reference proxy for the bit at \p Idx.
  reference operator[](unsigned Idx) {
    assert (Idx < Size && "Out-of-bounds Bit access.");
    return reference(*this, Idx);
  }

  /// Return the value of bit \p Idx.
  /// @param Idx Bit index.
  /// @return True if bit \p Idx is set.
  bool operator[](unsigned Idx) const {
    assert (Idx < Size && "Out-of-bounds Bit access.");
    BitWord Mask = BitWord(1) << (Idx % BITWORD_SIZE);
    return (Bits[Idx / BITWORD_SIZE] & Mask) != 0;
  }

  /// Return the last element in the bitvector.
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
  /// @return True if at least one bit in [Begin, End) is set.
  bool test_any(unsigned Begin, unsigned End) const {
    for (unsigned i = Begin; i < End; ++i) {
      if (test(i))
        return true;
    }
    return false;
  }

  /// Push a single bit onto the end of the bitvector.
  /// @param Val Bit value to append.
  void push_back(bool Val) {
    unsigned OldSize = Size;
    unsigned NewSize = Size + 1;

    // Resize, which will insert zeros.
    // If we already fit then the unused bits will be already zero.
    if (NewSize > getBitCapacity())
      resize(NewSize, false);
    else
      Size = NewSize;

    // If true, set single bit.
    if (Val)
      set(OldSize);
  }

  /// Pop one bit from the end of the vector.
  void pop_back() {
    assert(!empty() && "Empty vector has no element to pop.");
    resize(size() - 1);
  }

  /// Test if any common bits are set.
  /// @param RHS Bit vector to compare against for overlapping set bits.
  /// @return True if this and \p RHS share at least one set bit.
  bool anyCommon(const BitVector &RHS) const {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    for (unsigned i = 0, e = std::min(ThisWords, RHSWords); i != e; ++i)
      if (Bits[i] & RHS.Bits[i])
        return true;
    return false;
  }

  /// Return true if this and \p RHS have the same size and bit pattern.
  /// @param RHS Bit vector to compare against.
  /// @return True if both vectors have equal size and identical bits.
  bool operator==(const BitVector &RHS) const {
    if (size() != RHS.size())
      return false;
    unsigned NumWords = Bits.size();
    return std::equal(Bits.begin(), Bits.begin() + NumWords, RHS.Bits.begin());
  }

  /// Return true if this and \p RHS differ in size or bit pattern.
  /// @param RHS Bit vector to compare against.
  /// @return True if the vectors differ in size or bit pattern.
  bool operator!=(const BitVector &RHS) const { return !(*this == RHS); }

  /// Intersection of this bitvector with \p RHS.
  /// @param RHS Bit vector to AND with this one.
  /// @return Reference to this bit vector after the AND.
  BitVector &operator&=(const BitVector &RHS) {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    unsigned i;
    for (i = 0; i != std::min(ThisWords, RHSWords); ++i)
      Bits[i] &= RHS.Bits[i];

    // Any bits that are just in this bitvector become zero, because they aren't
    // in the RHS bit vector.  Any words only in RHS are ignored because they
    // are already zero in the LHS.
    for (; i != ThisWords; ++i)
      Bits[i] = 0;

    return *this;
  }

  /// Reset bits that are set in RHS. Same as *this &= ~RHS.
  /// @param RHS Bit vector whose set bits are cleared in this vector.
  /// @return Reference to this bit vector after clearing those bits.
  BitVector &reset(const BitVector &RHS) {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    for (unsigned i = 0; i != std::min(ThisWords, RHSWords); ++i)
      Bits[i] &= ~RHS.Bits[i];
    return *this;
  }

  /// Check if (This - RHS) is non-zero.
  /// This is the same as reset(RHS) and any().
  /// @param RHS Bit vector subtracted from this one before testing for any set bits.
  /// @return True if any bit is set in this vector but not in \p RHS.
  bool test(const BitVector &RHS) const {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    unsigned i;
    for (i = 0; i != std::min(ThisWords, RHSWords); ++i)
      if ((Bits[i] & ~RHS.Bits[i]) != 0)
        return true;

    for (; i != ThisWords ; ++i)
      if (Bits[i] != 0)
        return true;

    return false;
  }

  /// Check if This is a subset of RHS.
  /// @param RHS Bit vector that should contain every set bit of this vector.
  /// @return True if every set bit of this vector is also set in \p RHS.
  bool subsetOf(const BitVector &RHS) const { return !test(RHS); }

  /// Apply word-wise functor \p f to \p Arg and \p Args, storing the result
  /// in \p Out. All input vectors must have the same size.
  /// @param f Functor taking one word from each input vector.
  /// @param Out Destination bit vector (resized to match \p Arg).
  /// @param Arg First input bit vector.
  /// @param Args Additional input bit vectors of the same size.
  /// @return Reference to \p Out.
  template <class F, class... ArgTys>
  static BitVector &apply(F &&f, BitVector &Out, BitVector const &Arg,
                          ArgTys const &...Args) {
    assert(((Arg.size() == Args.size()) && ...) && "consistent sizes");
    Out.resize(Arg.size());
    for (size_type I = 0, E = Arg.Bits.size(); I != E; ++I)
      Out.Bits[I] = f(Arg.Bits[I], Args.Bits[I]...);
    Out.clear_unused_bits();
    return Out;
  }

  /// Union of this bitvector with \p RHS.
  /// @param RHS Bit vector to OR into this one.
  /// @return Reference to this bit vector after the OR.
  BitVector &operator|=(const BitVector &RHS) {
    if (size() < RHS.size())
      resize(RHS.size());
    for (size_type I = 0, E = RHS.Bits.size(); I != E; ++I)
      Bits[I] |= RHS.Bits[I];
    return *this;
  }

  /// Disjoint union of this bitvector with \p RHS.
  /// @param RHS Bit vector to XOR into this one.
  /// @return Reference to this bit vector after the XOR.
  BitVector &operator^=(const BitVector &RHS) {
    if (size() < RHS.size())
      resize(RHS.size());
    for (size_type I = 0, E = RHS.Bits.size(); I != E; ++I)
      Bits[I] ^= RHS.Bits[I];
    return *this;
  }

  /// Shift all bits right by \p N positions, filling vacated bits with zeros.
  /// @param N Number of bit positions to shift (must be <= size()).
  /// @return Reference to this bit vector after the shift.
  BitVector &operator>>=(unsigned N) {
    assert(N <= Size);
    if (LLVM_UNLIKELY(empty() || N == 0))
      return *this;

    unsigned NumWords = Bits.size();
    assert(NumWords >= 1);

    wordShr(N / BITWORD_SIZE);

    unsigned BitDistance = N % BITWORD_SIZE;
    if (BitDistance == 0)
      return *this;

    // When the shift size is not a multiple of the word size, then we have
    // a tricky situation where each word in succession needs to extract some
    // of the bits from the next word and or them into this word while
    // shifting this word to make room for the new bits.  This has to be done
    // for every word in the array.

    // Since we're shifting each word right, some bits will fall off the end
    // of each word to the right, and empty space will be created on the left.
    // The final word in the array will lose bits permanently, so starting at
    // the beginning, work forwards shifting each word to the right, and
    // OR'ing in the bits from the end of the next word to the beginning of
    // the current word.

    // Example:
    //   Starting with {0xAABBCCDD, 0xEEFF0011, 0x22334455} and shifting right
    //   by 4 bits.
    // Step 1: Word[0] >>= 4           ; 0x0ABBCCDD
    // Step 2: Word[0] |= 0x10000000   ; 0x1ABBCCDD
    // Step 3: Word[1] >>= 4           ; 0x0EEFF001
    // Step 4: Word[1] |= 0x50000000   ; 0x5EEFF001
    // Step 5: Word[2] >>= 4           ; 0x02334455
    // Result: { 0x1ABBCCDD, 0x5EEFF001, 0x02334455 }
    const BitWord Mask = maskTrailingOnes<BitWord>(BitDistance);
    const unsigned LSH = BITWORD_SIZE - BitDistance;

    for (unsigned I = 0; I < NumWords - 1; ++I) {
      Bits[I] >>= BitDistance;
      Bits[I] |= (Bits[I + 1] & Mask) << LSH;
    }

    Bits[NumWords - 1] >>= BitDistance;

    return *this;
  }

  /// Shift bits left by \p N positions; vacated low bits become zero.
  /// @param N Number of bit positions to shift; must be <= size().
  /// @return Reference to this bit vector after the shift.
  BitVector &operator<<=(unsigned N) {
    assert(N <= Size);
    if (LLVM_UNLIKELY(empty() || N == 0))
      return *this;

    unsigned NumWords = Bits.size();
    assert(NumWords >= 1);

    wordShl(N / BITWORD_SIZE);

    unsigned BitDistance = N % BITWORD_SIZE;
    if (BitDistance == 0)
      return *this;

    // When the shift size is not a multiple of the word size, then we have
    // a tricky situation where each word in succession needs to extract some
    // of the bits from the previous word and or them into this word while
    // shifting this word to make room for the new bits.  This has to be done
    // for every word in the array.  This is similar to the algorithm outlined
    // in operator>>=, but backwards.

    // Since we're shifting each word left, some bits will fall off the end
    // of each word to the left, and empty space will be created on the right.
    // The first word in the array will lose bits permanently, so starting at
    // the end, work backwards shifting each word to the left, and OR'ing
    // in the bits from the end of the next word to the beginning of the
    // current word.

    // Example:
    //   Starting with {0xAABBCCDD, 0xEEFF0011, 0x22334455} and shifting left
    //   by 4 bits.
    // Step 1: Word[2] <<= 4           ; 0x23344550
    // Step 2: Word[2] |= 0x0000000E   ; 0x2334455E
    // Step 3: Word[1] <<= 4           ; 0xEFF00110
    // Step 4: Word[1] |= 0x0000000A   ; 0xEFF0011A
    // Step 5: Word[0] <<= 4           ; 0xABBCCDD0
    // Result: { 0xABBCCDD0, 0xEFF0011A, 0x2334455E }
    const BitWord Mask = maskLeadingOnes<BitWord>(BitDistance);
    const unsigned RSH = BITWORD_SIZE - BitDistance;

    for (int I = NumWords - 1; I > 0; --I) {
      Bits[I] <<= BitDistance;
      Bits[I] |= (Bits[I - 1] & Mask) >> RSH;
    }
    Bits[0] <<= BitDistance;
    clear_unused_bits();

    return *this;
  }

  /// Exchange bit storage and size with \p RHS.
  /// @param RHS Other bit vector to swap with.
  void swap(BitVector &RHS) {
    std::swap(Bits, RHS.Bits);
    std::swap(Size, RHS.Size);
  }

  /// Return an ArrayRef to the underlying \c BitWord storage.
  /// @return ArrayRef covering the allocated BitWord storage.
  ArrayRef<BitWord> getData() const { return {Bits.data(), Bits.size()}; }

  //===--------------------------------------------------------------------===//
  // Portable bit mask operations.
  //===--------------------------------------------------------------------===//
  //
  // These methods all operate on arrays of uint32_t, each holding 32 bits. The
  // fixed word size makes it easier to work with literal bit vector constants
  // in portable code.
  //
  // The LSB in each word is the lowest numbered bit.  The size of a portable
  // bit mask is always a whole multiple of 32 bits.  If no bit mask size is
  // given, the bit mask is assumed to cover the entire BitVector.

  /// Add '1' bits from Mask to this vector. Don't resize.
  /// This computes "*this |= Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void setBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<true, false>(Mask, MaskWords);
  }

  /// Clear any bits in this vector that are set in Mask.
  /// Don't resize. This computes "*this &= ~Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void clearBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<false, false>(Mask, MaskWords);
  }

  /// Add a bit to this vector for every '0' bit in Mask.
  /// Don't resize.  This computes "*this |= ~Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void setBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<true, true>(Mask, MaskWords);
  }

  /// Clear a bit in this vector for every '0' bit in Mask.
  /// Don't resize.  This computes "*this &= Mask".
  /// @param Mask Portable bit mask of 32-bit words (LSB is lowest bit).
  /// @param MaskWords Number of words in \p Mask; defaults to covering this vector.
  void clearBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<false, true>(Mask, MaskWords);
  }

private:
  /// Perform a logical left shift of \p Count words by moving everything
  /// \p Count words to the right in memory.
  ///
  /// While confusing, words are stored from least significant at Bits[0] to
  /// most significant at Bits[NumWords-1].  A logical shift left, however,
  /// moves the current least significant bit to a higher logical index, and
  /// fills the previous least significant bits with 0.  Thus, we actually
  /// need to move the bytes of the memory to the right, not to the left.
  /// Example:
  ///   Words = [0xBBBBAAAA, 0xDDDDFFFF, 0x00000000, 0xDDDD0000]
  /// represents a BitVector where 0xBBBBAAAA contain the least significant
  /// bits.  So if we want to shift the BitVector left by 2 words, we need
  /// to turn this into 0x00000000 0x00000000 0xBBBBAAAA 0xDDDDFFFF by using a
  /// memmove which moves right, not left.
  void wordShl(uint32_t Count) {
    if (Count == 0)
      return;

    uint32_t NumWords = Bits.size();

    // Since we always move Word-sized chunks of data with src and dest both
    // aligned to a word-boundary, we don't need to worry about endianness
    // here.
    std::copy(Bits.begin(), Bits.begin() + NumWords - Count,
              Bits.begin() + Count);
    std::fill(Bits.begin(), Bits.begin() + Count, 0);
    clear_unused_bits();
  }

  /// Perform a logical right shift of \p Count words by moving those
  /// words to the left in memory.  See wordShl for more information.
  ///
  void wordShr(uint32_t Count) {
    if (Count == 0)
      return;

    uint32_t NumWords = Bits.size();

    std::copy(Bits.begin() + Count, Bits.begin() + NumWords, Bits.begin());
    std::fill(Bits.begin() + NumWords - Count, Bits.begin() + NumWords, 0);
  }

  unsigned NumBitWords(unsigned S) const {
    return (S + BITWORD_SIZE-1) / BITWORD_SIZE;
  }

  // Set the unused bits in the high words.
  void set_unused_bits(bool t = true) {
    //  Then set any stray high bits of the last used word.
    if (unsigned ExtraBits = Size % BITWORD_SIZE) {
      BitWord ExtraBitMask = ~BitWord(0) << ExtraBits;
      if (t)
        Bits.back() |= ExtraBitMask;
      else
        Bits.back() &= ~ExtraBitMask;
    }
  }

  // Clear the unused bits in the high words.
  void clear_unused_bits() {
    set_unused_bits(false);
  }

  void init_words(bool t) { llvm::fill(Bits, 0 - (BitWord)t); }

  template<bool AddBits, bool InvertMask>
  void applyMask(const uint32_t *Mask, unsigned MaskWords) {
    static_assert(BITWORD_SIZE % 32 == 0, "Unsupported BitWord size.");
    MaskWords = std::min(MaskWords, (size() + 31) / 32);
    const unsigned Scale = BITWORD_SIZE / 32;
    unsigned i;
    for (i = 0; MaskWords >= Scale; ++i, MaskWords -= Scale) {
      BitWord BW = Bits[i];
      // This inner loop should unroll completely when BITWORD_SIZE > 32.
      for (unsigned b = 0; b != BITWORD_SIZE; b += 32) {
        uint32_t M = *Mask++;
        if (InvertMask) M = ~M;
        if (AddBits) BW |=   BitWord(M) << b;
        else         BW &= ~(BitWord(M) << b);
      }
      Bits[i] = BW;
    }
    for (unsigned b = 0; MaskWords; b += 32, --MaskWords) {
      uint32_t M = *Mask++;
      if (InvertMask) M = ~M;
      if (AddBits) Bits[i] |=   BitWord(M) << b;
      else         Bits[i] &= ~(BitWord(M) << b);
    }
    if (AddBits)
      clear_unused_bits();
  }

public:
  /// Return the size (in bytes) of the bit vector.
  /// @return Number of bytes of allocated BitWord storage.
  size_type getMemorySize() const { return Bits.size() * sizeof(BitWord); }
  /// Return the number of bits that fit in the allocated storage.
  /// @return Bit capacity of the allocated storage.
  size_type getBitCapacity() const { return Bits.size() * BITWORD_SIZE; }
};

/// Return the number of bytes of storage allocated by \p X.
/// @param X Bit vector whose allocated storage size is queried.
/// @return Number of bytes of storage allocated by \p X.
inline BitVector::size_type capacity_in_bytes(const BitVector &X) {
  return X.getMemorySize();
}

/// Provide DenseMapInfo for BitVector, hashing size and word storage.
template <> struct DenseMapInfo<BitVector> {
  /// Compute a hash code for bit vector \p V.
  /// @param V Bit vector to hash.
  /// @return Hash code derived from \p V's size and word storage.
  static unsigned getHashValue(const BitVector &V) {
    return DenseMapInfo<std::pair<BitVector::size_type, ArrayRef<uintptr_t>>>::
        getHashValue(std::make_pair(V.size(), V.getData()));
  }
  /// Return true if \p LHS and \p RHS have equal size and bit pattern.
  /// @param LHS First bit vector.
  /// @param RHS Second bit vector.
  /// @return True if \p LHS and \p RHS compare equal.
  static bool isEqual(const BitVector &LHS, const BitVector &RHS) {
    return LHS == RHS;
  }
};
} // end namespace llvm

namespace std {
  /// Implement std::swap in terms of BitVector swap.
inline void swap(llvm::BitVector &LHS, llvm::BitVector &RHS) { LHS.swap(RHS); }
} // end namespace std

#endif // LLVM_ADT_BITVECTOR_H
