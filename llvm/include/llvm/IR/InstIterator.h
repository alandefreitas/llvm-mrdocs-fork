//===- InstIterator.h - Classes for inst iteration --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions of two iterators for iterating over the
// instructions in a function.  This is effectively a wrapper around a two level
// iterator that can probably be genericized later.
//
// Note that this iterator gets invalidated any time that basic blocks or
// instructions are moved around.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTITERATOR_H
#define LLVM_IR_INSTITERATOR_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include <iterator>

namespace llvm {

// This class implements inst_begin() & inst_end() for
// inst_iterator and const_inst_iterator's.
//
template <class BB_t, class BB_i_t, class BI_t, class II_t> class InstIterator {
  using BBty = BB_t;
  using BBIty = BB_i_t;
  using BIty = BI_t;
  using IIty = II_t;
  BB_t *BBs; // BasicBlocksType
  BB_i_t BB; // BasicBlocksType::iterator
  BI_t BI;   // BasicBlock::iterator

public:
  /// Bidirectional traversal category.
  using iterator_category = std::bidirectional_iterator_tag;
  /// Instruction type referred to by this iterator.
  using value_type = IIty;
  /// Signed distance between iterators.
  using difference_type = signed;
  /// Pointer to an instruction.
  using pointer = IIty *;
  /// Reference to an instruction.
  using reference = IIty &;

  /// Construct a singular (empty) iterator.
  InstIterator() = default;

  /// Construct by converting from a compatible \c InstIterator \p II.
  ///
  /// \param II Source iterator to copy basic-block and instruction positions
  /// from.
  template<typename A, typename B, typename C, typename D>
  InstIterator(const InstIterator<A,B,C,D> &II)
    : BBs(II.BBs), BB(II.BB), BI(II.BI) {}

  /// Construct by converting from a compatible non-const \c InstIterator
  /// \p II.
  ///
  /// \param II Source iterator to copy basic-block and instruction positions
  /// from.
  template<typename A, typename B, typename C, typename D>
  InstIterator(InstIterator<A,B,C,D> &II)
    : BBs(II.BBs), BB(II.BB), BI(II.BI) {}

  /// Construct a begin iterator over the instructions in function-like \p m.
  ///
  /// \param m Function (or similar) whose basic-block list is walked.
  template<class M> InstIterator(M &m)
    : BBs(&m.getBasicBlockList()), BB(BBs->begin()) {    // begin ctor
    if (BB != BBs->end()) {
      BI = BB->begin();
      advanceToNextBB();
    }
  }

  /// Construct an end iterator for the instructions in function-like \p m.
  ///
  /// The unused \c bool parameter distinguishes this from the begin
  /// constructor.
  ///
  /// \param m Function (or similar) whose basic-block list is walked.
  /// \param AtEnd Unused tag distinguishing this from the begin constructor.
  template<class M> InstIterator(M &m, bool AtEnd)
    : BBs(&m.getBasicBlockList()), BB(BBs->end()) {    // end ctor
  }

  /// Return the underlying basic-block list iterator.
  /// \return Reference to the current basic-block list iterator.
  inline BBIty &getBasicBlockIterator()  { return BB; }
  /// Return the underlying instruction iterator within the current block.
  /// \return Reference to the current instruction iterator.
  inline BIty  &getInstructionIterator() { return BI; }

  /// Return a reference to the current instruction.
  /// \return Reference to the instruction currently pointed to.
  inline reference operator*()  const { return *BI; }
  /// Return a pointer to the current instruction.
  /// \return Pointer to the instruction currently pointed to.
  inline pointer operator->() const { return &operator*(); }

  /// Return true if this iterator equals \p y.
  ///
  /// \param y Iterator to compare against.
  /// \return True if both iterators refer to the same instruction position.
  inline bool operator==(const InstIterator &y) const {
    return BB == y.BB && (BB == BBs->end() || BI == y.BI);
  }
  /// Return true if this iterator is not equal to \p y.
  ///
  /// \param y Iterator to compare against.
  /// \return True if the iterators refer to different instruction positions.
  inline bool operator!=(const InstIterator& y) const {
    return !operator==(y);
  }

  /// Advance to the next instruction and return this iterator.
  /// \return Reference to this iterator after advancing.
  InstIterator& operator++() {
    ++BI;
    advanceToNextBB();
    return *this;
  }
  /// Advance to the next instruction, returning the previous position.
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return Copy of the iterator before advancing.
  inline InstIterator operator++(int Unused) {
    InstIterator tmp = *this; ++*this; return tmp;
  }

  /// Move to the previous instruction and return this iterator.
  /// \return Reference to this iterator after moving backward.
  InstIterator& operator--() {
    while (BB == BBs->end() || BI == BB->begin()) {
      --BB;
      BI = BB->end();
    }
    --BI;
    return *this;
  }
  /// Move to the previous instruction, returning the previous position.
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return Copy of the iterator before moving backward.
  inline InstIterator operator--(int Unused) {
    InstIterator tmp = *this; --*this; return tmp;
  }

private:
  inline void advanceToNextBB() {
    // The only way that the II could be broken is if it is now pointing to
    // the end() of the current BasicBlock and there are successor BBs.
    while (BI == BB->end()) {
      ++BB;
      if (BB == BBs->end()) break;
      BI = BB->begin();
    }
  }
};

/// Mutable iterator over all instructions in a Function.
using inst_iterator =
    InstIterator<SymbolTableList<BasicBlock>, Function::iterator,
                 BasicBlock::iterator, Instruction>;
/// Read-only iterator over all instructions in a Function.
using const_inst_iterator =
    InstIterator<const SymbolTableList<BasicBlock>,
                 Function::const_iterator, BasicBlock::const_iterator,
                 const Instruction>;
/// Range of mutable instruction iterators.
using inst_range = iterator_range<inst_iterator>;
/// Range of read-only instruction iterators.
using const_inst_range = iterator_range<const_inst_iterator>;

/// Return an iterator to the first instruction in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Begin iterator over the instructions in \p F.
inline inst_iterator inst_begin(Function *F) { return inst_iterator(*F); }
/// Return the end iterator for instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Past-the-end iterator for the instructions in \p F.
inline inst_iterator inst_end(Function *F)   { return inst_iterator(*F, true); }
/// Return a range over all instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Range covering every instruction in \p F.
inline inst_range instructions(Function *F) {
  return inst_range(inst_begin(F), inst_end(F));
}
/// Return a const iterator to the first instruction in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const begin iterator over the instructions in \p F.
inline const_inst_iterator inst_begin(const Function *F) {
  return const_inst_iterator(*F);
}
/// Return the const end iterator for instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const past-the-end iterator for the instructions in \p F.
inline const_inst_iterator inst_end(const Function *F) {
  return const_inst_iterator(*F, true);
}
/// Return a const range over all instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const range covering every instruction in \p F.
inline const_inst_range instructions(const Function *F) {
  return const_inst_range(inst_begin(F), inst_end(F));
}
/// Return an iterator to the first instruction in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Begin iterator over the instructions in \p F.
inline inst_iterator inst_begin(Function &F) { return inst_iterator(F); }
/// Return the end iterator for instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Past-the-end iterator for the instructions in \p F.
inline inst_iterator inst_end(Function &F)   { return inst_iterator(F, true); }
/// Return a range over all instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Range covering every instruction in \p F.
inline inst_range instructions(Function &F) {
  return inst_range(inst_begin(F), inst_end(F));
}
/// Return a const iterator to the first instruction in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const begin iterator over the instructions in \p F.
inline const_inst_iterator inst_begin(const Function &F) {
  return const_inst_iterator(F);
}
/// Return the const end iterator for instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const past-the-end iterator for the instructions in \p F.
inline const_inst_iterator inst_end(const Function &F) {
  return const_inst_iterator(F, true);
}
/// Return a const range over all instructions in \p F.
///
/// \param F Function whose instructions are iterated.
/// \return Const range covering every instruction in \p F.
inline const_inst_range instructions(const Function &F) {
  return const_inst_range(inst_begin(F), inst_end(F));
}

} // end namespace llvm

#endif // LLVM_IR_INSTITERATOR_H
