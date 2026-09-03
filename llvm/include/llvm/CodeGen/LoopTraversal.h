//==------ llvm/CodeGen/LoopTraversal.h - Loop Traversal -*- C++ -*---------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Loop Traversal logic.
///
/// This class provides the basic blocks traversal order used by passes like
/// ReachingDefAnalysis and ExecutionDomainFix.
/// It identifies basic blocks that are part of loops and should to be visited
/// twice and returns efficient traversal order for all the blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOOPTRAVERSAL_H
#define LLVM_CODEGEN_LOOPTRAVERSAL_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;

/// Computes an efficient basic-block traversal order for loop-aware passes.
///
/// Provides the basic block traversal order used by passes like
/// ReachingDefAnalysis and ExecutionDomainFix. It identifies basic blocks
/// that are part of loops and should be visited twice and returns an
/// efficient traversal order for all the blocks.
///
/// We want to visit every instruction in every basic block in order to update
/// it's execution domain or collect clearance information. However, for the
/// clearance calculation, we need to know clearances from all predecessors
/// (including any backedges), therfore we need to visit some blocks twice.
/// As an example, consider the following loop.
///
///
///    PH -> A -> B (xmm<Undef> -> xmm<Def>) -> C -> D -> EXIT
///          ^                                  |
///          +----------------------------------+
///
/// The iteration order this pass will return is as follows:
/// Optimized: PH A B C A' B' C' D
///
/// The basic block order is constructed as follows:
/// Once we finish processing some block, we update the counters in MBBInfos
/// and re-process any successors that are now 'done'.
/// We call a block that is ready for its final round of processing `done`
/// (isBlockDone), e.g. when all predecessor information is known.
///
/// Note that a naive traversal order would be to do two complete passes over
/// all basic blocks/instructions, the first for recording clearances, the
/// second for updating clearance based on backedges.
/// However, for functions without backedges, or functions with a lot of
/// straight-line code, and a small loop, that would be a lot of unnecessary
/// work (since only the BBs that are part of the loop require two passes).
///
/// E.g., the naive iteration order for the above exmple is as follows:
/// Naive: PH A B C D A' B' C' D'
///
/// In the optimized approach we avoid processing D twice, because we
/// can entirely process the predecessors before getting to D.
class LoopTraversal {
private:
  struct MBBInfo {
    /// Whether we have gotten to this block in primary processing yet.
    bool PrimaryCompleted = false;

    /// The number of predecessors for which primary processing has completed
    unsigned IncomingProcessed = 0;

    /// The value of `IncomingProcessed` at the start of primary processing
    unsigned PrimaryIncoming = 0;

    /// The number of predecessors for which all processing steps are done.
    unsigned IncomingCompleted = 0;

    MBBInfo() = default;
  };
  using MBBInfoMap = SmallVector<MBBInfo, 4>;
  /// Helps keep track if we proccessed this block and all its predecessors.
  MBBInfoMap MBBInfos;

public:
  /// A machine basic block together with its traversal-pass status.
  struct TraversedMBBInfo {
    /// The basic block.
    MachineBasicBlock *MBB = nullptr;

    /// True if this is the first time we process the basic block.
    bool PrimaryPass = true;

    /// True if the block that is ready for its final round of processing.
    bool IsDone = true;

    /// Construct traversal info for a basic block.
    ///
    /// \param BB The machine basic block, or nullptr.
    /// \param Primary True if this is the primary (first) pass over the block.
    /// \param Done True if the block is ready for its final round of
    ///        processing.
    TraversedMBBInfo(MachineBasicBlock *BB = nullptr, bool Primary = true,
                     bool Done = true)
        : MBB(BB), PrimaryPass(Primary), IsDone(Done) {}
  };
  /// Construct an empty loop traversal helper.
  LoopTraversal() = default;

  /// Ordered list of basic blocks with primary-pass and done flags.
  typedef SmallVector<TraversedMBBInfo, 4> TraversalOrder;
  /// Compute an efficient traversal order of the basic blocks in \p MF.
  ///
  /// Identifies basic blocks that are part of loops and should be visited
  /// twice, and returns a traversal order that revisits only those blocks.
  ///
  /// \param MF The machine function whose blocks are traversed.
  /// \return Ordered list of blocks with primary-pass and done flags.
  LLVM_ABI TraversalOrder traverse(MachineFunction &MF);

private:
  /// Returens true if the block is ready for its final round of processing.
  bool isBlockDone(MachineBasicBlock *MBB);
};

} // namespace llvm

#endif // LLVM_CODEGEN_LOOPTRAVERSAL_H
