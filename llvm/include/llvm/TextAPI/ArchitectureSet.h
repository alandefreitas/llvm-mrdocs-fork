//===- llvm/TextAPI/ArchitectureSet.h - ArchitectureSet ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the architecture set.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_ARCHITECTURESET_H
#define LLVM_TEXTAPI_ARCHITECTURESET_H

#include "llvm/Support/Compiler.h"
#include "llvm/TextAPI/Architecture.h"
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

namespace llvm {
class raw_ostream;

namespace MachO {

/// Bitmask of Mach-O architecture slices.
class ArchitectureSet {
private:
  using ArchSetType = uint32_t;

  const static ArchSetType EndIndexVal =
      std::numeric_limits<ArchSetType>::max();
  ArchSetType ArchSet{0};

public:
  /// Construct an empty architecture set.
  constexpr ArchitectureSet() = default;
  /// Construct from a raw architecture-set bit mask.
  ///
  /// \param Raw Bit mask of architecture values.
  constexpr ArchitectureSet(ArchSetType Raw) : ArchSet(Raw) {}
  /// Construct a set containing a single architecture.
  ///
  /// \param Arch Architecture to include.
  ArchitectureSet(Architecture Arch) : ArchitectureSet() { set(Arch); }
  /// Construct a set from a list of architectures.
  ///
  /// \param Archs Architectures to include.
  LLVM_ABI ArchitectureSet(const std::vector<Architecture> &Archs);

  /// Return a set containing every architecture.
  ///
  /// \return Architecture set with every architecture bit set.
  static ArchitectureSet All() { return ArchitectureSet(EndIndexVal); }

  /// Add an architecture to the set.
  ///
  /// Unknown architectures are ignored.
  ///
  /// \param Arch Architecture to add.
  void set(Architecture Arch) {
    if (Arch == AK_unknown)
      return;
    ArchSet |= 1U << static_cast<int>(Arch);
  }

  /// Remove an architecture from the set.
  ///
  /// \param Arch Architecture to remove.
  /// \return The updated architecture set.
  ArchitectureSet clear(Architecture Arch) {
    ArchSet &= ~(1U << static_cast<int>(Arch));
    return ArchSet;
  }

  /// Check whether the set contains an architecture.
  ///
  /// \param Arch Architecture to test.
  /// \return True if \p Arch is present.
  bool has(Architecture Arch) const {
    return ArchSet & (1U << static_cast<int>(Arch));
  }

  /// Check whether this set contains every architecture in another set.
  ///
  /// \param Archs Architecture set that must be a subset.
  /// \return True if \p Archs is a subset of this set.
  bool contains(ArchitectureSet Archs) const {
    return (ArchSet & Archs.ArchSet) == Archs.ArchSet;
  }

  /// Return the number of architectures in the set.
  ///
  /// \return Number of architectures present.
  LLVM_ABI size_t count() const;

  /// Return true if the set contains no architectures.
  ///
  /// \return True if the set is empty.
  bool empty() const { return ArchSet == 0; }

  /// Return the raw architecture-set bit mask.
  ///
  /// \return Raw bit mask of architectures in the set.
  ArchSetType rawValue() const { return ArchSet; }

  /// Return true if the set contains any x86 architecture.
  ///
  /// \return True if any x86 architecture is present.
  bool hasX86() const {
    return has(AK_i386) || has(AK_x86_64) || has(AK_x86_64h);
  }

  /// Forward iterator over architectures present in an ArchitectureSet.
  template <typename Ty> class arch_iterator {
  public:
    /// Iterator category tag for a forward iterator.
    using iterator_category = std::forward_iterator_tag;
    /// Type of the architecture value yielded by the iterator.
    using value_type = Architecture;
    /// Type used to represent distances between iterators.
    using difference_type = std::size_t;
    /// Pointer type for the architecture value.
    using pointer = value_type *;
    /// Reference type for the architecture value.
    using reference = value_type &;

  private:
    ArchSetType Index;
    Ty *ArchSet;

    void findNextSetBit() {
      if (Index == EndIndexVal)
        return;
      while (++Index < sizeof(Ty) * 8) {
        if (*ArchSet & (1UL << Index))
          return;
      }

      Index = EndIndexVal;
    }

  public:
    /// Construct an iterator over \p ArchSet starting at \p Index.
    ///
    /// \param ArchSet Pointer to the architecture-set bit mask.
    /// \param Index Bit index to start from, or end sentinel.
    arch_iterator(Ty *ArchSet, ArchSetType Index = 0)
        : Index(Index), ArchSet(ArchSet) {
      if (Index != EndIndexVal && !(*ArchSet & (1UL << Index)))
        findNextSetBit();
    }

    /// Return the architecture at the current iterator position.
    ///
    /// \return Architecture at the current position.
    Architecture operator*() const { return static_cast<Architecture>(Index); }

    /// Advance to the next set architecture and return this iterator.
    ///
    /// \return Reference to this iterator after advancing.
    arch_iterator &operator++() {
      findNextSetBit();
      return *this;
    }

    /// Advance to the next set architecture and return the prior position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    arch_iterator operator++(int Unused) {
      auto tmp = *this;
      findNextSetBit();
      return tmp;
    }

    /// Return true if both iterators refer to the same position.
    ///
    /// \param o Iterator to compare against.
    /// \return True if both iterators refer to the same position.
    bool operator==(const arch_iterator &o) const {
      return std::tie(Index, ArchSet) == std::tie(o.Index, o.ArchSet);
    }

    /// Return true if the iterators refer to different positions.
    ///
    /// \param o Iterator to compare against.
    /// \return True if the iterators refer to different positions.
    bool operator!=(const arch_iterator &o) const { return !(*this == o); }
  };

  /// Return the intersection of this set and another.
  ///
  /// \param o Architecture set to intersect with.
  /// \return Intersection of the two sets.
  ArchitectureSet operator&(const ArchitectureSet &o) {
    return {ArchSet & o.ArchSet};
  }

  /// Return the union of this set and another.
  ///
  /// \param o Architecture set to unite with.
  /// \return Union of the two sets.
  ArchitectureSet operator|(const ArchitectureSet &o) {
    return {ArchSet | o.ArchSet};
  }

  /// Unite another architecture set into this set.
  ///
  /// \param o Architecture set to unite into this set.
  /// \return Reference to this set.
  ArchitectureSet &operator|=(const ArchitectureSet &o) {
    ArchSet |= o.ArchSet;
    return *this;
  }

  /// Add a single architecture to this set.
  ///
  /// \param Arch Architecture to add.
  /// \return Reference to this set.
  ArchitectureSet &operator|=(const Architecture &Arch) {
    set(Arch);
    return *this;
  }

  /// Return true if both sets contain the same architectures.
  ///
  /// \param o Architecture set to compare against.
  /// \return True if the sets are equal.
  bool operator==(const ArchitectureSet &o) const {
    return ArchSet == o.ArchSet;
  }

  /// Return true if the sets contain different architectures.
  ///
  /// \param o Architecture set to compare against.
  /// \return True if the sets are not equal.
  bool operator!=(const ArchitectureSet &o) const {
    return ArchSet != o.ArchSet;
  }

  /// Compare architecture sets by their raw bit masks.
  ///
  /// \param o Architecture set to compare against.
  /// \return True if this set's raw bit mask is less than \p o's.
  bool operator<(const ArchitectureSet &o) const { return ArchSet < o.ArchSet; }

  /// Mutable iterator over architectures in the set.
  using iterator = arch_iterator<ArchSetType>;
  /// Const iterator over architectures in the set.
  using const_iterator = arch_iterator<const ArchSetType>;

  /// Iterator to the first architecture in the set.
  ///
  /// \return Mutable iterator to the first architecture.
  iterator begin() { return {&ArchSet}; }
  /// Past-the-end iterator for the architecture range.
  ///
  /// \return Mutable past-the-end iterator.
  iterator end() { return {&ArchSet, EndIndexVal}; }

  /// Const iterator to the first architecture in the set.
  ///
  /// \return Const iterator to the first architecture.
  const_iterator begin() const { return {&ArchSet}; }
  /// Past-the-end const iterator for the architecture range.
  ///
  /// \return Const past-the-end iterator.
  const_iterator end() const { return {&ArchSet, EndIndexVal}; }

  /// Convert the set to a space-separated string of architecture names.
  ///
  /// \return Space-separated architecture names.
  LLVM_ABI operator std::string() const;
  /// Convert the set to a vector of its architectures.
  ///
  /// \return Vector of architectures in the set.
  LLVM_ABI operator std::vector<Architecture>() const;
  /// Print the architectures in the set to a stream.
  ///
  /// \param OS Output stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;
};

/// Form the union of two single architectures as an ArchitectureSet.
///
/// \param lhs First architecture.
/// \param rhs Second architecture.
/// \return Architecture set containing both architectures.
inline ArchitectureSet operator|(const Architecture &lhs,
                                 const Architecture &rhs) {
  return ArchitectureSet(lhs) | ArchitectureSet(rhs);
}

/// Write an architecture set to a raw output stream.
///
/// \param OS Output stream to write to.
/// \param Set Architecture set to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, ArchitectureSet Set);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_ARCHITECTURESET_H
