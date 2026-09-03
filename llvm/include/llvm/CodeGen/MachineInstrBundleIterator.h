//===- llvm/CodeGen/MachineInstrBundleIterator.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines an iterator class that bundles MachineInstr.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H
#define LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/simple_ilist.h"
#include <cassert>
#include <iterator>
#include <type_traits>

namespace llvm {

/// Iterator traits for bundle-aware machine instruction iterators.
///
/// Selects forward or reverse \c simple_ilist iterators and const/non-const
/// variants used by \c MachineInstrBundleIterator.
template <class T, bool IsReverse> struct MachineInstrBundleIteratorTraits;
/// Forward non-const traits for element type \p T.
template <class T> struct MachineInstrBundleIteratorTraits<T, false> {
  /// Underlying instruction list type.
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  /// Forward iterator over instructions.
  using instr_iterator = typename list_type::iterator;
  /// Mutable forward instruction iterator.
  using nonconst_instr_iterator = typename list_type::iterator;
  /// Const forward instruction iterator.
  using const_instr_iterator = typename list_type::const_iterator;
};
/// Reverse non-const traits for element type \p T.
template <class T> struct MachineInstrBundleIteratorTraits<T, true> {
  /// Underlying instruction list type.
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  /// Reverse iterator over instructions.
  using instr_iterator = typename list_type::reverse_iterator;
  /// Mutable reverse instruction iterator.
  using nonconst_instr_iterator = typename list_type::reverse_iterator;
  /// Const reverse instruction iterator.
  using const_instr_iterator = typename list_type::const_reverse_iterator;
};
/// Forward const traits for element type \p T.
template <class T> struct MachineInstrBundleIteratorTraits<const T, false> {
  /// Underlying instruction list type.
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  /// Const forward iterator over instructions.
  using instr_iterator = typename list_type::const_iterator;
  /// Mutable forward instruction iterator.
  using nonconst_instr_iterator = typename list_type::iterator;
  /// Const forward instruction iterator.
  using const_instr_iterator = typename list_type::const_iterator;
};
/// Reverse const traits for element type \p T.
template <class T> struct MachineInstrBundleIteratorTraits<const T, true> {
  /// Underlying instruction list type.
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  /// Const reverse iterator over instructions.
  using instr_iterator = typename list_type::const_reverse_iterator;
  /// Mutable reverse instruction iterator.
  using nonconst_instr_iterator = typename list_type::reverse_iterator;
  /// Const reverse instruction iterator.
  using const_instr_iterator = typename list_type::const_reverse_iterator;
};

/// Helper selecting bundle walk direction for forward or reverse iterators.
template <bool IsReverse> struct MachineInstrBundleIteratorHelper;
/// Forward-direction helpers for walking instruction bundles.
template <> struct MachineInstrBundleIteratorHelper<false> {
  /// Get the beginning of the current bundle.
  /// @param I Instruction iterator into a bundle.
  /// @return Iterator to the first instruction in the bundle containing \p I.
  template <class Iterator> static Iterator getBundleBegin(Iterator I) {
    if (!I.isEnd())
      while (I->isBundledWithPred())
        --I;
    return I;
  }

  /// Get the final node of the current bundle.
  /// @param I Instruction iterator into a bundle.
  /// @return Iterator to the last instruction in the bundle containing \p I.
  template <class Iterator> static Iterator getBundleFinal(Iterator I) {
    if (!I.isEnd())
      while (I->isBundledWithSucc())
        ++I;
    return I;
  }

  /// Increment forward ilist iterator.
  /// @param I Forward iterator to advance past the current bundle.
  template <class Iterator> static void increment(Iterator &I) {
    I = std::next(getBundleFinal(I));
  }

  /// Decrement forward ilist iterator.
  /// @param I Forward iterator to retreat to the previous bundle.
  template <class Iterator> static void decrement(Iterator &I) {
    I = getBundleBegin(std::prev(I));
  }
};

/// Reverse-direction helpers for walking instruction bundles.
template <> struct MachineInstrBundleIteratorHelper<true> {
  /// Get the beginning of the current bundle.
  /// @param I Reverse instruction iterator into a bundle.
  /// @return Reverse iterator to the first instruction in the bundle
  /// containing \p I.
  template <class Iterator> static Iterator getBundleBegin(Iterator I) {
    return MachineInstrBundleIteratorHelper<false>::getBundleBegin(
               I.getReverse())
        .getReverse();
  }

  /// Get the final node of the current bundle.
  /// @param I Reverse instruction iterator into a bundle.
  /// @return Reverse iterator to the last instruction in the bundle
  /// containing \p I.
  template <class Iterator> static Iterator getBundleFinal(Iterator I) {
    return MachineInstrBundleIteratorHelper<false>::getBundleFinal(
               I.getReverse())
        .getReverse();
  }

  /// Increment reverse ilist iterator.
  /// @param I Reverse iterator to advance past the current bundle.
  template <class Iterator> static void increment(Iterator &I) {
    I = getBundleBegin(std::next(I));
  }

  /// Decrement reverse ilist iterator.
  /// @param I Reverse iterator to retreat to the previous bundle.
  template <class Iterator> static void decrement(Iterator &I) {
    I = std::prev(getBundleFinal(I));
  }
};

/// MachineBasicBlock iterator that automatically skips over MIs that are
/// inside bundles (i.e. walk top level MIs only).
template <typename Ty, bool IsReverse = false>
class MachineInstrBundleIterator : MachineInstrBundleIteratorHelper<IsReverse> {
  using Traits = MachineInstrBundleIteratorTraits<Ty, IsReverse>;
  using instr_iterator = typename Traits::instr_iterator;

  instr_iterator MII;

public:
  /// Instruction type referred to by this iterator.
  using value_type = typename instr_iterator::value_type;
  /// Signed distance between iterators.
  using difference_type = typename instr_iterator::difference_type;
  /// Pointer to an instruction.
  using pointer = typename instr_iterator::pointer;
  /// Reference to an instruction.
  using reference = typename instr_iterator::reference;
  /// Const pointer to an instruction.
  using const_pointer = typename instr_iterator::const_pointer;
  /// Const reference to an instruction.
  using const_reference = typename instr_iterator::const_reference;
  /// Bidirectional traversal category.
  using iterator_category = std::bidirectional_iterator_tag;

private:
  using nonconst_instr_iterator = typename Traits::nonconst_instr_iterator;
  using const_instr_iterator = typename Traits::const_instr_iterator;
  using nonconst_iterator =
      MachineInstrBundleIterator<typename nonconst_instr_iterator::value_type,
                                 IsReverse>;
  using reverse_iterator = MachineInstrBundleIterator<Ty, !IsReverse>;

public:
  /// Construct from an instruction-list iterator at a bundle boundary.
  /// @param MI Iterator to an unbundled instruction or end.
  MachineInstrBundleIterator(instr_iterator MI) : MII(MI) {
    assert((!MI.getNodePtr() || MI.isEnd() || !MI->isBundledWithPred()) &&
           "It's not legal to initialize MachineInstrBundleIterator with a "
           "bundled MI");
  }

  /// Construct from an instruction reference at a bundle boundary.
  /// @param MI Unbundled instruction this iterator should refer to.
  MachineInstrBundleIterator(reference MI) : MII(MI) {
    assert(!MI.isBundledWithPred() && "It's not legal to initialize "
                                      "MachineInstrBundleIterator with a "
                                      "bundled MI");
  }

  /// Construct from an instruction pointer at a bundle boundary.
  /// @param MI Pointer to an unbundled instruction, or null.
  MachineInstrBundleIterator(pointer MI) : MII(MI) {
    // FIXME: This conversion should be explicit.
    assert((!MI || !MI->isBundledWithPred()) && "It's not legal to initialize "
                                                "MachineInstrBundleIterator "
                                                "with a bundled MI");
  }

  /// Construct from a convertible bundle iterator (e.g. non-const to const).
  /// @param I Source bundle iterator to copy from.
  /// @param EnableIf SFINAE discriminator enabling convertible element types
  /// only.
  // Template allows conversion from const to nonconst.
  template <class OtherTy>
  MachineInstrBundleIterator(
      const MachineInstrBundleIterator<OtherTy, IsReverse> &I,
      std::enable_if_t<std::is_convertible<OtherTy *, Ty *>::value, void *>
          EnableIf = nullptr)
      : MII(I.getInstrIterator()) {}

  /// Construct a singular (null) bundle iterator.
  MachineInstrBundleIterator() : MII(nullptr) {}

  /// Explicit conversion between forward/reverse iterators.
  ///
  /// Translate between forward and reverse iterators without changing range
  /// boundaries.  The resulting iterator will dereference (and have a handle)
  /// to the previous node, which is somewhat unexpected; but converting the
  /// two endpoints in a range will give the same range in reverse.
  ///
  /// This matches std::reverse_iterator conversions.
  /// @param I Opposite-direction iterator to convert from.
  explicit MachineInstrBundleIterator(
      const MachineInstrBundleIterator<Ty, !IsReverse> &I)
      : MachineInstrBundleIterator(++I.getReverse()) {}

  /// Get the bundle iterator for the given instruction's bundle.
  /// @param MI Instruction-list iterator into a bundle.
  /// @return Bundle iterator positioned at the beginning of \p MI's bundle.
  static MachineInstrBundleIterator getAtBundleBegin(instr_iterator MI) {
    return MachineInstrBundleIteratorHelper<IsReverse>::getBundleBegin(MI);
  }

  /// Dereference to the instruction at this bundle position.
  /// @return Reference to the instruction at this bundle position.
  reference operator*() const { return *MII; }
  /// Member access for the instruction at this bundle position.
  /// @return Pointer to the instruction at this bundle position.
  pointer operator->() const { return &operator*(); }

  /// Check for null.
  /// @return True if this iterator points at an instruction (not singular).
  bool isValid() const { return MII.getNodePtr(); }

  /// Return true if \p L and \p R refer to the same bundle position.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if \p L and \p R refer to the same bundle position.
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const MachineInstrBundleIterator &R) {
    return L.MII == R.MII;
  }
  /// Return true if \p L equals const instruction iterator \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand const instruction iterator.
  /// @return True if \p L equals const instruction iterator \p R.
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const const_instr_iterator &R) {
    return L.MII == R; // Avoid assertion about validity of R.
  }
  /// Return true if const instruction iterator \p L equals \p R.
  /// @param L Left-hand const instruction iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if const instruction iterator \p L equals \p R.
  friend bool operator==(const const_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return L == R.MII; // Avoid assertion about validity of L.
  }
  /// Return true if \p L equals non-const instruction iterator \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand non-const instruction iterator.
  /// @return True if \p L equals non-const instruction iterator \p R.
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const nonconst_instr_iterator &R) {
    return L.MII == R; // Avoid assertion about validity of R.
  }
  /// Return true if non-const instruction iterator \p L equals \p R.
  /// @param L Left-hand non-const instruction iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if non-const instruction iterator \p L equals \p R.
  friend bool operator==(const nonconst_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return L == R.MII; // Avoid assertion about validity of L.
  }
  /// Compare a bundle iterator to an instruction pointer.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand instruction pointer.
  /// @return True if \p L equals instruction pointer \p R.
  friend bool operator==(const MachineInstrBundleIterator &L, const_pointer R) {
    return L == const_instr_iterator(R); // Avoid assertion about validity of R.
  }
  /// Return true if instruction pointer \p L equals bundle iterator \p R.
  /// @param L Left-hand instruction pointer.
  /// @param R Right-hand bundle iterator.
  /// @return True if instruction pointer \p L equals bundle iterator \p R.
  friend bool operator==(const_pointer L, const MachineInstrBundleIterator &R) {
    return const_instr_iterator(L) == R; // Avoid assertion about validity of L.
  }
  /// Return true if bundle iterator \p L refers to instruction \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand instruction reference.
  /// @return True if bundle iterator \p L refers to instruction \p R.
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const_reference R) {
    return L == &R; // Avoid assertion about validity of R.
  }
  /// Compare a machine instruction reference with a bundle iterator.
  /// @param L Left-hand instruction reference.
  /// @param R Right-hand bundle iterator.
  /// @return True if instruction reference \p L equals bundle iterator \p R.
  friend bool operator==(const_reference L,
                         const MachineInstrBundleIterator &R) {
    return &L == R; // Avoid assertion about validity of L.
  }

  /// Return true if \p L and \p R refer to different bundle positions.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if \p L and \p R refer to different bundle positions.
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  /// Return true if \p L differs from const instruction iterator \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand const instruction iterator.
  /// @return True if \p L differs from const instruction iterator \p R.
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const const_instr_iterator &R) {
    return !(L == R);
  }
  /// Return true if const instruction iterator \p L differs from \p R.
  /// @param L Left-hand const instruction iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if const instruction iterator \p L differs from \p R.
  friend bool operator!=(const const_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  /// Return true if \p L differs from non-const instruction iterator \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand non-const instruction iterator.
  /// @return True if \p L differs from non-const instruction iterator \p R.
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const nonconst_instr_iterator &R) {
    return !(L == R);
  }
  /// Return true if non-const instruction iterator \p L differs from \p R.
  /// @param L Left-hand non-const instruction iterator.
  /// @param R Right-hand bundle iterator.
  /// @return True if non-const instruction iterator \p L differs from \p R.
  friend bool operator!=(const nonconst_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  /// Return true if \p L does not equal instruction pointer \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand instruction pointer.
  /// @return True if \p L does not equal instruction pointer \p R.
  friend bool operator!=(const MachineInstrBundleIterator &L, const_pointer R) {
    return !(L == R);
  }
  /// Return true if instruction pointer \p L does not equal \p R.
  /// @param L Left-hand instruction pointer.
  /// @param R Right-hand bundle iterator.
  /// @return True if instruction pointer \p L does not equal \p R.
  friend bool operator!=(const_pointer L, const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  /// Return true if the bundle iterator does not refer to instruction \p R.
  /// @param L Left-hand bundle iterator.
  /// @param R Right-hand instruction reference.
  /// @return True if the bundle iterator does not refer to instruction \p R.
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const_reference R) {
    return !(L == R);
  }
  /// Return true if the machine instruction reference and bundle iterator do
  /// not refer to the same instruction.
  /// @param L Left-hand instruction reference.
  /// @param R Right-hand bundle iterator.
  /// @return True if \p L and \p R do not refer to the same instruction.
  friend bool operator!=(const_reference L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }

  /// Move to the previous top-level instruction (bundle).
  /// @return Reference to this iterator after retreating.
  MachineInstrBundleIterator &operator--() {
    this->decrement(MII);
    return *this;
  }
  /// Move to the next top-level instruction (bundle).
  /// @return Reference to this iterator after advancing.
  MachineInstrBundleIterator &operator++() {
    this->increment(MII);
    return *this;
  }
  /// Post-decrement to the previous top-level instruction (bundle).
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before retreating.
  MachineInstrBundleIterator operator--(int Unused) {
    MachineInstrBundleIterator Temp = *this;
    --*this;
    return Temp;
  }
  /// Post-increment to the next top-level instruction (bundle).
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before advancing.
  MachineInstrBundleIterator operator++(int Unused) {
    MachineInstrBundleIterator Temp = *this;
    ++*this;
    return Temp;
  }

  /// Return the underlying instruction-list iterator for this bundle position.
  /// @return The underlying instruction-list iterator.
  instr_iterator getInstrIterator() const { return MII; }

  /// Return a non-const bundle iterator to the same position.
  /// @return Non-const bundle iterator to the same position.
  nonconst_iterator getNonConstIterator() const { return MII.getNonConst(); }

  /// Get a reverse iterator to the same node.
  ///
  /// Gives a reverse iterator that will dereference (and have a handle) to the
  /// same node.  Converting the endpoint iterators in a range will give a
  /// different range; for range operations, use the explicit conversions.
  /// @return Reverse iterator referring to the same node.
  reverse_iterator getReverse() const { return MII.getReverse(); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H
