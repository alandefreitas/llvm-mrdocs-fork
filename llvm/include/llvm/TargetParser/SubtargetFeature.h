//=== llvm/TargetParser/SubtargetFeature.h - CPU characteristics-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Defines and manages user or tool specified CPU characteristics.
/// The intent is to be able to package specific features that should or should
/// not be used on a specific target processor.  A tool, such as llc, could, as
/// as example, gather chip info from the command line, a long with features
/// that should be used on that chip.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_SUBTARGETFEATURE_H
#define LLVM_TARGETPARSER_SUBTARGETFEATURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
class Triple;

/// Number of 64-bit words used to store subtarget feature bits.
const unsigned MAX_SUBTARGET_WORDS = 7;
/// Maximum number of subtarget feature bits (MAX_SUBTARGET_WORDS * 64).
const unsigned MAX_SUBTARGET_FEATURES = MAX_SUBTARGET_WORDS * 64;

/// Container class for subtarget features.
///
/// This is a constexpr reimplementation of a subset of std::bitset. It would be
/// nice to use std::bitset directly, but it doesn't support constant
/// initialization.
class FeatureBitset {
  static_assert((MAX_SUBTARGET_FEATURES % 64) == 0,
                "Should be a multiple of 64!");
  std::array<uint64_t, MAX_SUBTARGET_WORDS> Bits{};

protected:
  /// Construct from a raw array of feature words.
  /// @param B Feature words copied into this bitset.
  constexpr FeatureBitset(const std::array<uint64_t, MAX_SUBTARGET_WORDS> &B)
      : Bits{B} {}

public:
  /// Construct an all-zero feature bitset.
  constexpr FeatureBitset() = default;
  /// Construct a bitset with each listed bit index set.
  /// @param Init Bit indices to set.
  constexpr FeatureBitset(std::initializer_list<unsigned> Init) {
    for (auto I : Init)
      set(I);
  }

  /// Set every bit to one and return this bitset.
  /// @return Reference to this bitset after setting all bits.
  FeatureBitset &set() {
    llvm::fill(Bits, -1ULL);
    return *this;
  }

  /// Set bit \p I and return this bitset.
  /// @param I Bit index to set.
  /// @return Reference to this bitset after setting bit \p I.
  constexpr FeatureBitset &set(unsigned I) {
    Bits[I / 64] |= uint64_t(1) << (I % 64);
    return *this;
  }

  /// Clear bit \p I and return this bitset.
  /// @param I Bit index to clear.
  /// @return Reference to this bitset after clearing bit \p I.
  constexpr FeatureBitset &reset(unsigned I) {
    Bits[I / 64] &= ~(uint64_t(1) << (I % 64));
    return *this;
  }

  /// Toggle bit \p I and return this bitset.
  /// @param I Bit index to flip.
  /// @return Reference to this bitset after flipping bit \p I.
  constexpr FeatureBitset &flip(unsigned I) {
    Bits[I / 64] ^= uint64_t(1) << (I % 64);
    return *this;
  }

  /// Return the value of bit \p I.
  /// @param I Bit index to read.
  /// @return True if bit \p I is set.
  constexpr bool operator[](unsigned I) const {
    uint64_t Mask = uint64_t(1) << (I % 64);
    return (Bits[I / 64] & Mask) != 0;
  }

  /// Return true if bit \p I is set.
  /// @param I Bit index to test.
  /// @return True if bit \p I is one.
  constexpr bool test(unsigned I) const { return (*this)[I]; }

  /// Return the fixed number of bits in this bitset.
  /// @return Maximum number of subtarget feature bits.
  constexpr size_t size() const { return MAX_SUBTARGET_FEATURES; }

  /// Index of the first set bit at or after Begin, or size() if none.
  /// @param Begin Bit index to start searching from.
  /// @return Index of the first set bit at or after \p Begin, or size() if none.
  unsigned find_first_from(unsigned Begin) const {
    for (unsigned Word = Begin / 64; Word < Bits.size(); ++Word) {
      uint64_t Masked = Bits[Word] & maskTrailingZeros<uint64_t>(Begin % 64);
      if (Masked)
        return Word * 64 + llvm::countr_zero(Masked);
      Begin = (Word + 1) * 64;
    }
    return size();
  }

  /// Yields the index of each set bit, skipping unset bits via countr_zero.
  class const_iterator
      : public iterator_facade_base<const_iterator, std::forward_iterator_tag,
                                    const unsigned, std::ptrdiff_t,
                                    const unsigned *, unsigned> {
    const FeatureBitset *Parent = nullptr;
    unsigned Index = 0;

  public:
    /// Construct a singular iterator.
    const_iterator() = default;
    /// Construct an iterator at bit \p Index of \p Parent.
    /// @param Parent Bitset being iterated.
    /// @param Index Current set-bit index, or size() for end.
    const_iterator(const FeatureBitset &Parent, unsigned Index)
        : Parent(&Parent), Index(Index) {}

    /// Return the current set-bit index.
    /// @return Index of the current set bit in the parent bitset.
    unsigned operator*() const { return Index; }
    /// Advance to the next set bit.
    /// @return Reference to this iterator after advancing.
    const_iterator &operator++() {
      Index = Parent->find_first_from(Index + 1);
      return *this;
    }
    /// Return true if this iterator and \p RHS are at the same index.
    /// @param RHS Iterator to compare with.
    /// @return True if both iterators are at the same index.
    bool operator==(const const_iterator &RHS) const {
      return Index == RHS.Index;
    }
  };

  /// Return an iterator to the first set bit, or end() if none.
  /// @return Iterator positioned at the first set bit, or end() if none.
  const_iterator begin() const {
    return const_iterator(*this, find_first_from(0));
  }
  /// Return an iterator past the last bit.
  /// @return Past-the-end iterator for set-bit iteration.
  const_iterator end() const { return const_iterator(*this, size()); }

  /// Return true if at least one bit is set.
  /// @return True if any bit is one.
  bool any() const {
    return llvm::any_of(Bits, [](uint64_t I) { return I != 0; });
  }
  /// Return true if no bits are set.
  /// @return True if every bit is clear.
  bool none() const { return !any(); }
  /// Return the number of bits that are set.
  /// @return Count of bits that are one.
  size_t count() const {
    size_t Count = 0;
    for (auto B : Bits)
      Count += llvm::popcount(B);
    return Count;
  }

  /// XOR each word with \p RHS in place.
  /// @param RHS Bitset to XOR with.
  /// @return Reference to this bitset after the XOR.
  constexpr FeatureBitset &operator^=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I) {
      Bits[I] ^= RHS.Bits[I];
    }
    return *this;
  }
  /// Return the bitwise XOR of this bitset and \p RHS.
  /// @param RHS Bitset to XOR with.
  /// @return New bitset that is the bitwise XOR of this and \p RHS.
  constexpr FeatureBitset operator^(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result ^= RHS;
    return Result;
  }

  /// AND each word with \p RHS in place.
  /// @param RHS Bitset to AND with.
  /// @return Reference to this bitset after the AND.
  constexpr FeatureBitset &operator&=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I)
      Bits[I] &= RHS.Bits[I];
    return *this;
  }
  /// Return the bitwise AND of this bitset and \p RHS.
  /// @param RHS Bitset to AND with.
  /// @return New bitset that is the bitwise AND of this and \p RHS.
  constexpr FeatureBitset operator&(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result &= RHS;
    return Result;
  }

  /// OR each word with \p RHS in place.
  /// @param RHS Bitset to OR with.
  /// @return Reference to this bitset after the OR.
  constexpr FeatureBitset &operator|=(const FeatureBitset &RHS) {
    for (unsigned I = 0, E = Bits.size(); I != E; ++I) {
      Bits[I] |= RHS.Bits[I];
    }
    return *this;
  }
  /// Return the bitwise OR of this bitset and \p RHS.
  /// @param RHS Bitset to OR with.
  /// @return New bitset that is the bitwise OR of this and \p RHS.
  constexpr FeatureBitset operator|(const FeatureBitset &RHS) const {
    FeatureBitset Result = *this;
    Result |= RHS;
    return Result;
  }

  /// Return a bitset with every bit inverted.
  /// @return New bitset that is the bitwise complement of this one.
  constexpr FeatureBitset operator~() const {
    FeatureBitset Result = *this;
    for (auto &B : Result.Bits)
      B = ~B;
    return Result;
  }

  /// Return true if every word matches \p RHS.
  /// @param RHS Bitset to compare with.
  /// @return True if this bitset equals \p RHS.
  bool operator==(const FeatureBitset &RHS) const {
    return std::equal(std::begin(Bits), std::end(Bits), std::begin(RHS.Bits));
  }

  /// Return true if any word differs from \p RHS.
  /// @param RHS Bitset to compare with.
  /// @return True if this bitset is not equal to \p RHS.
  bool operator!=(const FeatureBitset &RHS) const { return !(*this == RHS); }

  /// Lexicographically compare bits against \p Other (false < true).
  /// @param Other Bitset to compare with.
  /// @return True if this bitset is lexicographically less than \p Other.
  bool operator < (const FeatureBitset &Other) const {
    for (unsigned I = 0, E = size(); I != E; ++I) {
      bool LHS = test(I), RHS = Other.test(I);
      if (LHS != RHS)
        return LHS < RHS;
    }
    return false;
  }
};

/// Class used to store the subtarget bits in the tables created by tablegen.
class FeatureBitArray : public FeatureBitset {
public:
  /// Construct from TableGen feature words.
  /// @param B Feature words copied into this array.
  constexpr FeatureBitArray(const std::array<uint64_t, MAX_SUBTARGET_WORDS> &B)
      : FeatureBitset(B) {}

  /// Return this array as a FeatureBitset.
  /// @return Const reference to this object as a FeatureBitset.
  const FeatureBitset &getAsBitset() const { return *this; }
};

//===----------------------------------------------------------------------===//

/// Manages the enabling and disabling of subtarget specific features.
///
/// Features are encoded as a string of the form
///   "+attr1,+attr2,-attr3,...,+attrN"
/// A comma separates each feature from the next (all lowercase.)
/// Each of the remaining features is prefixed with + or - indicating whether
/// that feature should be enabled or disabled contrary to the cpu
/// specification.
class SubtargetFeatures {
  std::vector<std::string> Features;    ///< Subtarget features as a vector

public:
  /// Construct from an optional comma-separated feature string.
  /// @param Initial Feature string of the form "+attr1,+attr2,-attr3".
  LLVM_ABI explicit SubtargetFeatures(StringRef Initial = "");

  /// Returns features as a string.
  /// @return Comma-separated feature string with leading '+' or '-' flags.
  LLVM_ABI std::string getString() const;

  /// Adds Features.
  /// @param String Feature name, with or without a leading '+' or '-' flag.
  /// @param Enable If true, prefix with '+'; if false, prefix with '-'.
  LLVM_ABI void AddFeature(StringRef String, bool Enable = true);

  /// Append every feature string from \p OtherFeatures.
  /// @param OtherFeatures Feature strings to append.
  LLVM_ABI void addFeaturesVector(const ArrayRef<std::string> OtherFeatures);

  /// Returns the vector of individual subtarget features.
  /// @return Const reference to the stored feature strings.
  const std::vector<std::string> &getFeatures() const { return Features; }

  /// Prints feature string.
  /// @param OS Stream to write the feature string to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Dumps feature info.
  LLVM_ABI void dump() const;

  /// Adds the default features for the specified target triple.
  /// @param Triple Target triple whose default features are added.
  LLVM_ABI void getDefaultSubtargetFeatures(const Triple &Triple);

  /// Determine if a feature has a flag; '+' or '-'
  /// @param Feature Feature string to inspect.
  /// @return True if \p Feature begins with '+' or '-'.
  static bool hasFlag(StringRef Feature) {
    assert(!Feature.empty() && "Empty string");
    // Get first character
    char Ch = Feature[0];
    // Check if first character is '+' or '-' flag
    return Ch == '+' || Ch =='-';
  }

  /// Return string stripped of flag.
  /// @param Feature Feature string that may have a leading '+' or '-'.
  /// @return \p Feature without a leading '+' or '-', or \p Feature unchanged.
  static StringRef StripFlag(StringRef Feature) {
    return hasFlag(Feature) ? Feature.substr(1) : Feature;
  }

  /// Return true if enable flag; '+'.
  /// @param Feature Feature string whose leading flag is tested.
  /// @return True if \p Feature is prefixed with '+'.
  static inline bool isEnabled(StringRef Feature) {
    assert(!Feature.empty() && "Empty string");
    // Get first character
    char Ch = Feature[0];
    // Check if first character is '+' for enabled
    return Ch == '+';
  }

  /// Splits a string of comma separated items in to a vector of strings.
  /// @param V Destination vector that receives the split items.
  /// @param S Comma-separated string to split.
  LLVM_ABI static void Split(std::vector<std::string> &V, StringRef S);
};

} // end namespace llvm

#endif // LLVM_TARGETPARSER_SUBTARGETFEATURE_H
