//===- SuspendCrossingInfo.cpp - Utility for suspend crossing values ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The SuspendCrossingInfo maintains data that allows to answer a question
// whether given two BasicBlocks A and B there is a path from A to B that
// passes through a suspend point. Note, SuspendCrossingInfo is invalidated
// by changes to the CFG including adding/removing BBs due to its use of BB
// ptrs in the BlockToIndexMapping.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_SUSPENDCROSSINGINFO_H
#define LLVM_TRANSFORMS_COROUTINES_SUSPENDCROSSINGINFO_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Coroutines/CoroInstr.h"
#include "llvm/Transforms/Coroutines/CoroShape.h"

namespace llvm {

class ModuleSlotTracker;

/// Bidirectional mapping between a function's basic blocks and dense indices.
class BlockToIndexMapping {
  SmallVector<BasicBlock *, 32> V;

public:
  /// Return the number of mapped basic blocks.
  ///
  /// \return The number of mapped basic blocks.
  size_t size() const { return V.size(); }

  /// Build a mapping covering every basic block in \p F.
  ///
  /// \param F Function whose basic blocks are indexed.
  BlockToIndexMapping(Function &F) {
    for (BasicBlock &BB : F)
      V.push_back(&BB);
    llvm::sort(V);
  }

  /// Return the dense index assigned to \p BB.
  ///
  /// \param BB Basic block that must belong to this mapping.
  /// \return The dense index assigned to \p BB.
  size_t blockToIndex(BasicBlock const *BB) const {
    auto *I = llvm::lower_bound(V, BB);
    assert(I != V.end() && *I == BB && "BasicBlockNumberng: Unknown block");
    return I - V.begin();
  }

  /// Return the basic block stored at \p Index.
  ///
  /// \param Index Dense index previously returned by blockToIndex.
  /// \return The basic block stored at \p Index.
  BasicBlock *indexToBlock(unsigned Index) const { return V[Index]; }
};

/// Answers whether a path between two basic blocks crosses a suspend point.
///
/// For every basic block 'i' it maintains a BlockData that consists of:
///   Consumes:  a bit vector which contains a set of indices of blocks that can
///              reach block 'i'. A block can trivially reach itself.
///   Kills: a bit vector which contains a set of indices of blocks that can
///          reach block 'i' but there is a path crossing a suspend point
///          not repeating 'i' (path to 'i' without cycles containing 'i').
///   AlwaysKill: a boolean indicating whether block 'i' always propagate kills.
///   NeverKill: a boolean indicating whether block 'i' never propagate kills.
///   KillLoop: There is a path from 'i' to 'i' not otherwise repeating 'i' that
///             crosses a suspend point.
///
/// Invalidated by CFG changes that add or remove basic blocks, because
/// BlockToIndexMapping stores basic-block pointers.
class SuspendCrossingInfo {
  BlockToIndexMapping Mapping;

  struct BlockData {
    BitVector Consumes;
    BitVector Kills;
    bool KillLoop = false;
    bool Changed = false;

  private:
    bool AlwaysKill = false;
    bool NeverKill = false;

  public:
    bool isAlwaysKill() const { return AlwaysKill; }
    bool isNeverKill() const { return NeverKill; }
    void setAlwaysKill() {
      AlwaysKill = true;
      NeverKill = false;
    }
    void setNeverKill() {
      AlwaysKill = false;
      NeverKill = true;
    }
  };
  SmallVector<BlockData, 32> Block;

  iterator_range<pred_iterator> predecessors(BlockData const &BD) const {
    BasicBlock *BB = Mapping.indexToBlock(&BD - &Block[0]);
    return llvm::predecessors(BB);
  }

  BlockData &getBlockData(BasicBlock *BB) {
    return Block[Mapping.blockToIndex(BB)];
  }

  /// Compute the BlockData for the current function in one iteration.
  /// Initialize - Whether this is the first iteration, we can optimize
  /// the initial case a little bit by manual loop switch.
  /// Returns whether the BlockData changes in this iteration.
  template <bool Initialize = false>
  bool computeBlockData(const ReversePostOrderTraversal<Function *> &RPOT);

public:
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump consume and kill sets for every block in reverse post-order.
  void dump() const;
  /// Dump the blocks set in \p BV, prefixed by \p Label, in reverse
  /// post-order.
  ///
  /// \param Label Prefix printed before the block list.
  /// \param BV Bit vector indexed by BlockToIndexMapping, one bit per block.
  /// \param RPOT Reverse post-order used to print blocks.
  /// \param MST Slot tracker used to name unnamed blocks.
  void dump(StringRef Label, BitVector const &BV,
            const ReversePostOrderTraversal<Function *> &RPOT,
            ModuleSlotTracker &MST) const;
#endif

  /// Compute suspend-crossing data for \p F using the coroutine \p Shape.
  ///
  /// \param F Function whose CFG is analyzed.
  /// \param Shape Coroutine shape providing suspend, save, and end points.
  LLVM_ABI
  SuspendCrossingInfo(Function &F, const coro::Shape &Shape);

  /// Returns true if there is a path from \p From to \p To crossing a suspend
  /// point without crossing \p From a 2nd time.
  ///
  /// \param From Block at the start of the path.
  /// \param To Block at the end of the path.
  /// \return true if such a path exists.
  LLVM_ABI bool hasPathCrossingSuspendPoint(BasicBlock *From,
                                            BasicBlock *To) const;

  /// Returns true if a path or loop from \p From to \p To crosses a suspend.
  ///
  /// Returns true if there is a path from \p From to \p To crossing a suspend
  /// point without crossing \p From a 2nd time. If \p From is the same as \p To
  /// this will also check if there is a looping path crossing a suspend point.
  ///
  /// \param From Block at the start of the path.
  /// \param To Block at the end of the path.
  /// \return true if such a path or loop exists.
  LLVM_ABI bool hasPathOrLoopCrossingSuspendPoint(BasicBlock *From,
                                                  BasicBlock *To) const;

  /// Returns true if a definition in \p DefBB is used by \p U across a
  /// suspend.
  ///
  /// PHI nodes with more than one incoming value are ignored. Uses by
  /// llvm.coro.suspend.retcon or llvm.coro.suspend.async are treated as
  /// occurring in the suspend's single predecessor.
  ///
  /// \param DefBB Block that defines the value.
  /// \param U User of the defined value.
  /// \return true if the use crosses a suspend point.
  bool isDefinitionAcrossSuspend(BasicBlock *DefBB, User *U) const {
    auto *I = cast<Instruction>(U);

    // We rewrote PHINodes, so that only the ones with exactly one incoming
    // value need to be analyzed.
    if (auto *PN = dyn_cast<PHINode>(I))
      if (PN->getNumIncomingValues() > 1)
        return false;

    BasicBlock *UseBB = I->getParent();

    // As a special case, treat uses by an llvm.coro.suspend.retcon or an
    // llvm.coro.suspend.async as if they were uses in the suspend's single
    // predecessor: the uses conceptually occur before the suspend.
    if (isa<CoroSuspendRetconInst>(I) || isa<CoroSuspendAsyncInst>(I)) {
      UseBB = UseBB->getSinglePredecessor();
      assert(UseBB && "should have split coro.suspend into its own block");
    }

    return hasPathCrossingSuspendPoint(DefBB, UseBB);
  }

  /// Returns true if argument \p A is used by \p U across a suspend.
  ///
  /// Treats the argument as defined in the function's entry block.
  ///
  /// \param A Function argument whose uses are checked.
  /// \param U User of \p A.
  /// \return true if the use crosses a suspend point.
  bool isDefinitionAcrossSuspend(Argument &A, User *U) const {
    return isDefinitionAcrossSuspend(&A.getParent()->getEntryBlock(), U);
  }

  /// Returns true if instruction \p I is used by \p U across a suspend.
  ///
  /// Values produced by llvm.coro.suspend.* are treated as defined in the
  /// suspend's single successor.
  ///
  /// \param I Instruction that defines the value.
  /// \param U User of \p I.
  /// \return true if the use crosses a suspend point.
  bool isDefinitionAcrossSuspend(Instruction &I, User *U) const {
    auto *DefBB = I.getParent();

    // As a special case, treat values produced by an llvm.coro.suspend.*
    // as if they were defined in the single successor: the uses
    // conceptually occur after the suspend.
    if (isa<AnyCoroSuspendInst>(I)) {
      DefBB = DefBB->getSingleSuccessor();
      assert(DefBB && "should have split coro.suspend into its own block");
    }

    return isDefinitionAcrossSuspend(DefBB, U);
  }

  /// Returns true if value \p V is used by \p U across a suspend.
  ///
  /// \param V Definition to check; must be an Argument or Instruction.
  /// \param U User of \p V.
  /// \return true if the use crosses a suspend point.
  bool isDefinitionAcrossSuspend(Value &V, User *U) const {
    if (auto *Arg = dyn_cast<Argument>(&V))
      return isDefinitionAcrossSuspend(*Arg, U);
    if (auto *Inst = dyn_cast<Instruction>(&V))
      return isDefinitionAcrossSuspend(*Inst, U);

    llvm_unreachable(
        "Coroutine could only collect Argument and Instruction now.");
  }

  /// Returns true if any use of \p V crosses a suspend point.
  ///
  /// \param V Definition to check; must be an Argument or Instruction.
  /// \return true if any use of \p V crosses a suspend point.
  bool isDefinitionAcrossSuspend(Value &V) const {
    if (auto *Arg = dyn_cast<Argument>(&V)) {
      for (User *U : Arg->users())
        if (isDefinitionAcrossSuspend(*Arg, U))
          return true;
    } else if (auto *Inst = dyn_cast<Instruction>(&V)) {
      for (User *U : Inst->users())
        if (isDefinitionAcrossSuspend(*Inst, U))
          return true;
    }

    llvm_unreachable(
        "Coroutine could only collect Argument and Instruction now.");
  }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_SUSPENDCROSSINGINFO_H
