//===- LockstepReverseIterator.h ------------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOCKSTEPREVERSEITERATOR_H
#define LLVM_TRANSFORMS_UTILS_LOCKSTEPREVERSEITERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

/// Empty base used when \c LockstepReverseIterator does not track active blocks.
struct NoActiveBlocksOption {};

/// Base that tracks which blocks are still participating in iteration.
struct ActiveBlocksOption {
  /// Blocks that still have instructions to visit.
  SmallSetVector<BasicBlock *, 4> ActiveBlocks;
  /// Return the set of blocks that are still active.
  ///
  /// \return The set of blocks that are still active.
  SmallSetVector<BasicBlock *, 4> &getActiveBlocks() { return ActiveBlocks; }
  /// Construct an empty active-blocks option.
  ActiveBlocksOption() = default;
};

/// Iterates instructions across blocks in reverse from the first non-terminator.
///
/// For example (assume all blocks have size n):
///   LockstepReverseIterator I([B1, B2, B3]);
///   *I-- = [B1[n], B2[n], B3[n]];
///   *I-- = [B1[n-1], B2[n-1], B3[n-1]];
///   *I-- = [B1[n-2], B2[n-2], B3[n-2]];
///   ...
///
/// The iterator continues processing until all blocks have been exhausted if \p
/// EarlyFailure is explicitly set to \c false. Use \c getActiveBlocks() to
/// determine which blocks are still going and the order they appear in the list
/// returned by operator*.
template <bool EarlyFailure = true>
class LockstepReverseIterator
    : private std::conditional_t<EarlyFailure, NoActiveBlocksOption,
                                 ActiveBlocksOption> {
private:
  using Base = std::conditional_t<EarlyFailure, NoActiveBlocksOption,
                                  ActiveBlocksOption>;
  ArrayRef<BasicBlock *> Blocks;
  SmallVector<Instruction *, 4> Insts;
  bool Fail;

public:
  /// Construct an iterator over \p Blocks starting at each first non-terminator.
  ///
  /// \param Blocks Blocks whose instructions are iterated in lockstep reverse.
  LockstepReverseIterator(ArrayRef<BasicBlock *> Blocks) : Blocks(Blocks) {
    reset();
  }

  /// Reset the iterator to the first non-terminator of each block.
  void reset() {
    Fail = false;
    if constexpr (!EarlyFailure) {
      this->ActiveBlocks.clear();
      this->ActiveBlocks.insert_range(Blocks);
    }
    Insts.clear();
    for (BasicBlock *BB : Blocks) {
      Instruction *Prev = BB->getTerminator()->getPrevNode();
      if (!Prev) {
        // Block wasn't big enough - only contained a terminator.
        if constexpr (EarlyFailure) {
          Fail = true;
          return;
        } else {
          this->ActiveBlocks.remove(BB);
          continue;
        }
      }
      Insts.push_back(Prev);
    }
    if (Insts.empty())
      Fail = true;
  }

  /// Return true if the iterator still points at a valid instruction set.
  ///
  /// \return True if the iterator is still valid.
  bool isValid() const { return !Fail; }
  /// Return the current instructions, one from each active block.
  ///
  /// \return The current instructions, one from each active block.
  ArrayRef<Instruction *> operator*() const { return Insts; }

  /// Return the blocks that are still active during non-early-failure iteration.
  ///
  /// Must return a SmallSetVector so later copies into Blocks via std::copy
  /// preserve a deterministic order that matches the corresponding Values.
  ///
  /// \return The set of blocks that are still active.
  SmallSetVector<BasicBlock *, 4> &getActiveBlocks() {
    return Base::getActiveBlocks();
  }

  /// Drop instructions whose parents are not in \p Blocks from the active set.
  ///
  /// \param Blocks Blocks allowed to remain in the active instruction set.
  void restrictToBlocks(SmallSetVector<BasicBlock *, 4> &Blocks) {
    static_assert(!EarlyFailure, "Unknown method");
    for (auto It = Insts.begin(); It != Insts.end();) {
      if (!Blocks.contains((*It)->getParent())) {
        this->ActiveBlocks.remove((*It)->getParent());
        It = Insts.erase(It);
      } else {
        ++It;
      }
    }
  }

  /// Move to the previous instruction in each active block.
  ///
  /// \return A reference to this iterator.
  LockstepReverseIterator &operator--() {
    if (Fail)
      return *this;
    SmallVector<Instruction *, 4> NewInsts;
    for (Instruction *Inst : Insts) {
      Instruction *Prev = Inst->getPrevNode();
      if (!Prev) {
        if constexpr (!EarlyFailure) {
          this->ActiveBlocks.remove(Inst->getParent());
        } else {
          Fail = true;
          return *this;
        }
      } else {
        NewInsts.push_back(Prev);
      }
    }
    if (NewInsts.empty())
      Fail = true;
    else
      Insts = NewInsts;
    return *this;
  }

  /// Move to the next instruction in each active block.
  ///
  /// \return A reference to this iterator.
  LockstepReverseIterator &operator++() {
    static_assert(EarlyFailure, "Unknown method");
    if (Fail)
      return *this;
    SmallVector<Instruction *, 4> NewInsts;
    for (Instruction *Inst : Insts) {
      Instruction *Next = Inst->getNextNode();
      // Already at end of block.
      if (!Next) {
        Fail = true;
        return *this;
      }
      NewInsts.push_back(Next);
    }
    if (NewInsts.empty())
      Fail = true;
    else
      Insts = NewInsts;
    return *this;
  }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOCKSTEPREVERSEITERATOR_H
