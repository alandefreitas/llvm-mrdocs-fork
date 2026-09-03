//===-- OMP.h - Core OpenMP definitions and declarations ---------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the core set of OpenMP definitions and declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMP_H
#define LLVM_FRONTEND_OPENMP_OMP_H

#include "llvm/Frontend/OpenMP/OMP.h.inc"
#include "llvm/Support/Compiler.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm::omp {
template <typename Enum, size_t Size> struct EnumSet;

namespace detail {
template <size_t Size>
static constexpr inline size_t findFirstSet(size_t Begin, size_t End,
                                            const llvm::Bitset<Size> &Set) {
  unsigned BeginWord = Begin / 64;
  unsigned EndWord = (End + 63) / 64;

  for (unsigned I = BeginWord; I < EndWord; ++I) {
    uint64_t Word = Set.getWord64(I);
    if (I == BeginWord && Begin % 64 != 0) {
      Word &= ~uint64_t() << (Begin % 64);
    }
    auto Count = static_cast<unsigned>(llvm::countr_zero_constexpr(Word));
    if (Count < 64) {
      unsigned Idx = I * 64 + Count;
      if (Idx >= Begin && Idx < End)
        return Idx;
    }
  }
  return Size;
}

template <typename Enum, size_t Size> struct EnumSetIterator {
  using value_type = Enum;
  static constexpr size_t enum_size = Size;

  constexpr EnumSetIterator(const EnumSet<Enum, Size> &Set, size_t At)
      : Set(Set), At(At) {}

  constexpr Enum operator*() const;
  constexpr auto &operator++();

  constexpr bool operator==(const EnumSetIterator<Enum, Size> &Other) const {
    return &Set == &Other.Set && At == Other.At;
  }
  constexpr bool operator!=(const EnumSetIterator<Enum, Size> &Other) const {
    return !operator==(Other);
  }

private:
  const EnumSet<Enum, Size> &Set;
  size_t At;
};
} // namespace detail

/// A set of enumeration values backed by a fixed-size bitset.
template <typename Enum, size_t Size>
struct EnumSet : public llvm::Bitset<Size> {
  /// The enumeration type stored in this set.
  using value_type = Enum;
  /// The underlying fixed-size bitset type.
  using Base = llvm::Bitset<Size>;
  /// Inherit constructors from \c Base.
  using Base::Base;
  /// Iterator over the enumeration values present in the set.
  using iterator = detail::EnumSetIterator<Enum, Size>;

  /// Construct from an existing bitset.
  /// @param B Bitset to move into this set.
  constexpr EnumSet(Base &&B) : Base(std::move(B)) {}
  /// Construct a set containing each listed enumeration value.
  /// @param Init Enumeration values to include.
  constexpr EnumSet(std::initializer_list<value_type> Init) {
    for (value_type E : Init) {
      auto Value = static_cast<unsigned>(E);
      assert(Value < Base::size() && "Invalid enumeration value");
      Base::set(Value);
    }
  }

  /// Return true if the set contains no enumeration values.
  /// @return True if the set is empty.
  constexpr bool empty() const { return Base::none(); }
  /// Return the number of enumeration values in the set.
  /// @return The number of enumeration values in the set.
  constexpr size_t size() const { return Base::count(); }
  /// Return the maximum number of distinct enumeration values.
  /// @return The maximum number of distinct enumeration values.
  constexpr size_t max_size() const { return Size; }

  /// Return true if \p E is present in the set.
  /// @param E Enumeration value to test.
  /// @return True if \p E is present in the set.
  constexpr bool test(Enum E) const {
    return Base::test(static_cast<unsigned>(E));
  }
  /// Return true if \p E is present in the set.
  /// @param E Enumeration value to test.
  /// @return True if \p E is present in the set.
  constexpr bool operator[](Enum E) const {
    return Base::operator[](static_cast<unsigned>(E));
  }
  /// Toggle membership of \p E and return this set.
  /// @param E Enumeration value to flip.
  /// @return This set.
  constexpr EnumSet &flip(Enum E) {
    Base::flip(static_cast<unsigned>(E));
    return *this;
  }
  /// Remove \p E from the set and return this set.
  /// @param E Enumeration value to clear.
  /// @return This set.
  constexpr EnumSet &reset(Enum E) {
    Base::reset(static_cast<unsigned>(E));
    return *this;
  }
  /// Insert \p E into the set and return this set.
  /// @param E Enumeration value to set.
  /// @return This set.
  constexpr EnumSet &set(Enum E) {
    Base::set(static_cast<unsigned>(E));
    return *this;
  }

  /// Union this set with \p S in place.
  /// @param S Set to OR with.
  /// @return This set.
  constexpr EnumSet &operator|=(const EnumSet &S) {
    Base::operator|=(S);
    return *this;
  }
  /// Intersect this set with \p S in place.
  /// @param S Set to AND with.
  /// @return This set.
  constexpr EnumSet &operator&=(const EnumSet &S) {
    Base::operator&=(S);
    return *this;
  }
  /// Return the union of this set and \p S.
  /// @param S Set to OR with.
  /// @return A new set with the union of this set and \p S.
  constexpr EnumSet operator|(const EnumSet &S) const {
    EnumSet T{*this};
    return T |= S;
  }
  /// Return the intersection of this set and \p S.
  /// @param S Set to AND with.
  /// @return A new set with the intersection of this set and \p S.
  constexpr EnumSet operator&(const EnumSet &S) const {
    EnumSet T{*this};
    return T &= S;
  }

  /// Return an iterator to the first present enumeration value.
  /// @return An iterator to the first present enumeration value.
  constexpr iterator begin() const {
    return iterator(*this, detail::findFirstSet<Size>(0, Size, *this));
  }
  /// Return a past-the-end iterator for the set.
  /// @return A past-the-end iterator for the set.
  constexpr iterator end() const { return iterator(*this, Size); }
};

namespace detail {
template <typename Enum, size_t Size>
constexpr Enum EnumSetIterator<Enum, Size>::operator*() const {
  // Older gcc doesn't like assert(Set.Base::test(At));
  assert((static_cast<const llvm::Bitset<Size> &>(Set).test(At)));
  return static_cast<Enum>(At);
}

template <typename Enum, size_t Size>
constexpr auto &EnumSetIterator<Enum, Size>::operator++() {
  At = findFirstSet<Size>(At + 1, Size, Set);
  return *this;
}
} // namespace detail

/// Set of OpenMP clause identifiers.
using ClauseSet = EnumSet<llvm::omp::Clause, llvm::omp::Clause_enumSize>;
/// Set of OpenMP directive identifiers.
using DirectiveSet =
    EnumSet<llvm::omp::Directive, llvm::omp::Directive_enumSize>;

/// Return the leaf constructs that make up compound directive \p D.
/// @param D Directive whose leaf constructs are requested.
/// @return The leaf constructs that make up \p D.
LLVM_ABI ArrayRef<Directive> getLeafConstructs(Directive D);
/// Return the leaf constructs of \p D, or \p D itself if it is a leaf.
/// @param D Directive whose leaf constructs are requested.
/// @return The leaf constructs of \p D, or \p D itself if it is a leaf.
LLVM_ABI ArrayRef<Directive> getLeafConstructsOrSelf(Directive D);

/// Append the leaf and composite constituents of \p D to \p Output.
/// @param D Directive to decompose into leaf and composite constructs.
/// @param Output Destination for the constituent directives.
/// @return A view of the leaf and composite constituents written to \p Output.
LLVM_ABI ArrayRef<Directive>
getLeafOrCompositeConstructs(Directive D, SmallVectorImpl<Directive> &Output);

/// Return the compound directive formed by \p Parts, or \c OMPD_unknown.
/// @param Parts Leaf or compound directives to combine, in order.
/// @return The compound directive formed by \p Parts, or \c OMPD_unknown.
LLVM_ABI Directive getCompoundConstruct(ArrayRef<Directive> Parts);

/// Return true if \p D is a leaf construct (not combined or composite).
/// @param D Directive to classify.
/// @return True if \p D is a leaf construct.
LLVM_ABI bool isLeafConstruct(Directive D);
/// Return true if \p D is a composite construct per the OpenMP spec.
/// @param D Directive to classify.
/// @return True if \p D is a composite construct.
LLVM_ABI bool isCompositeConstruct(Directive D);
/// Return true if \p D is a combined construct per the OpenMP spec.
/// @param D Directive to classify.
/// @return True if \p D is a combined construct.
LLVM_ABI bool isCombinedConstruct(Directive D);

static constexpr inline auto clauses() {
  return llvm::enum_seq_inclusive(Clause::First_, Clause::Last_);
}

static constexpr inline auto directives() {
  return llvm::enum_seq_inclusive(Directive::First_, Directive::Last_);
}

/// Can clause C have an iterator-modifier.
static constexpr inline bool canHaveIterator(Clause C) {
  // [5.2:67:5]
  switch (C) {
  case OMPC_affinity:
  case OMPC_depend:
  case OMPC_from:
  case OMPC_map:
  case OMPC_to:
    return true;
  default:
    return false;
  }
}

// Can clause C create a private copy of a variable.
static constexpr inline bool isPrivatizingClause(Clause C, unsigned Version) {
  switch (C) {
  case OMPC_firstprivate:
  case OMPC_in_reduction:
  case OMPC_lastprivate:
  case OMPC_linear:
  case OMPC_private:
  case OMPC_reduction:
  case OMPC_task_reduction:
    return true;
  case OMPC_detach:
  case OMPC_induction:
  case OMPC_is_device_ptr:
  case OMPC_use_device_ptr:
    return Version >= 60;
  default:
    return false;
  }
}

static constexpr inline bool isDataSharingAttributeClause(Clause C,
                                                          unsigned Version) {
  // The "Version" parameter is in case the result is version-depenent
  // in the future.
  (void)Version;
  switch (C) {
  case OMPC_detach:
  case OMPC_firstprivate:
  case OMPC_has_device_addr:
  case OMPC_induction:
  case OMPC_in_reduction:
  case OMPC_is_device_ptr:
  case OMPC_lastprivate:
  case OMPC_linear:
  case OMPC_private:
  case OMPC_reduction:
  case OMPC_shared:
  case OMPC_task_reduction:
  case OMPC_use_device_addr:
  case OMPC_use_device_ptr:
  case OMPC_uses_allocators:
    return true;
  default:
    return false;
  }
}

static constexpr inline bool isEndClause(Clause C) {
  switch (C) {
  case OMPC_copyprivate:
  case OMPC_nowait:
    return true;
  default:
    return false;
  }
}

static constexpr unsigned FallbackVersion = 52;
/// Return the OpenMP language versions supported by this frontend.
/// @return The OpenMP language versions supported by this frontend.
LLVM_ABI ArrayRef<unsigned> getOpenMPVersions();

/// Can directive D, under some circumstances, create a private copy
/// of a variable in given OpenMP version?
/// @param D Directive to check for privatizing behavior.
/// @param Version OpenMP language version to evaluate against.
/// @return True if \p D can create a private copy under some circumstances.
LLVM_ABI bool isPrivatizingConstruct(Directive D, unsigned Version);

/// Return the reserved OpenMP locator names (lowercase).
/// @return The reserved OpenMP locator names (lowercase).
LLVM_ABI ArrayRef<StringRef> getReservedLocatorNames();

/// Create a nicer version of a function name for humans to look at.
/// @param FunctionName Mangled or kernel function name to prettify.
/// @return A human-readable version of \p FunctionName.
LLVM_ABI std::string prettifyFunctionName(StringRef FunctionName);

/// Deconstruct an OpenMP kernel name into the parent function name and the line
/// number.
/// @param KernelName OpenMP offloading kernel symbol name to parse.
/// @param LineNo Set to the source line number encoded in the kernel name.
/// @return The parent function name extracted from \p KernelName.
LLVM_ABI std::string deconstructOpenMPKernelName(StringRef KernelName,
                                                 unsigned &LineNo);

} // namespace llvm::omp

#endif // LLVM_FRONTEND_OPENMP_OMP_H
