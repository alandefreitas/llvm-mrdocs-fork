//===- Transform/Utils/CodeMoverUtils.h - CodeMover Utils -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions determine movements are safe on basic blocks, and
// instructions contained within a function.
//
// Please note that this is work in progress, and the functionality is not
// ready for broader production use.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H
#define LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class DependenceInfo;
class DominatorTree;
class Instruction;
class PostDominatorTree;
class ScalarEvolution;

/// Return true if \p I can be safely moved before \p InsertPoint.
///
/// \param I Instruction to move.
/// \param InsertPoint Instruction before which \p I would be placed.
/// \param DT Dominator tree for the function.
/// \param PDT Optional post-dominator tree; required for a true result.
/// \param DI Optional dependence info; required for a true result.
/// \param CheckForEntireBlock Whether to relax checks when moving a whole
///        block.
/// \return True if \p I can be safely moved before \p InsertPoint.
LLVM_ABI bool isSafeToMoveBefore(Instruction &I, Instruction &InsertPoint,
                                 DominatorTree &DT,
                                 const PostDominatorTree *PDT = nullptr,
                                 DependenceInfo *DI = nullptr,
                                 bool CheckForEntireBlock = false);

/// Return true if all instructions (except the terminator) in \p BB can be
/// safely moved before \p InsertPoint.
///
/// \param BB Basic block whose non-terminator instructions would move.
/// \param InsertPoint Instruction before which the instructions would be
///        placed.
/// \param DT Dominator tree for the function.
/// \param PDT Optional post-dominator tree; required for a true result.
/// \param DI Optional dependence info; required for a true result.
/// \return True if all non-terminator instructions in \p BB can be safely
///         moved before \p InsertPoint.
LLVM_ABI bool isSafeToMoveBefore(BasicBlock &BB, Instruction &InsertPoint,
                                 DominatorTree &DT,
                                 const PostDominatorTree *PDT = nullptr,
                                 DependenceInfo *DI = nullptr);

/// Move instructions, in an order-preserving manner, from \p FromBB to the
/// beginning of \p ToBB when proven safe.
///
/// \param FromBB Block whose safe instructions are moved.
/// \param ToBB Destination block that receives the moved instructions.
/// \param DT Dominator tree for the function.
/// \param PDT Post-dominator tree for the function.
/// \param DI Dependence information used for safety checks.
/// \param SE Scalar evolution analysis invalidated for moved values.
LLVM_ABI void
moveInstructionsToTheBeginning(BasicBlock &FromBB, BasicBlock &ToBB,
                               DominatorTree &DT, const PostDominatorTree &PDT,
                               DependenceInfo &DI, ScalarEvolution &SE);

/// Move instructions, in an order-preserving manner, from \p FromBB to the end
/// of \p ToBB when proven safe.
///
/// \param FromBB Block whose safe instructions are moved.
/// \param ToBB Destination block that receives the moved instructions.
/// \param DT Dominator tree for the function.
/// \param PDT Post-dominator tree for the function.
/// \param DI Dependence information used for safety checks.
/// \param SE Scalar evolution analysis invalidated for moved values.
LLVM_ABI void moveInstructionsToTheEnd(BasicBlock &FromBB, BasicBlock &ToBB,
                                       DominatorTree &DT,
                                       const PostDominatorTree &PDT,
                                       DependenceInfo &DI, ScalarEvolution &SE);

/// Return true if \p ThisBlock is reached after \p OtherBlock in the CFG.
///
/// In case that two BBs \p ThisBlock and \p OtherBlock are control flow
/// equivalent but they do not strictly dominate and post-dominate each
/// other, we determine if \p ThisBlock is reached after \p OtherBlock
/// in the control flow.
///
/// \param ThisBlock Block that may be reached later.
/// \param OtherBlock Block that may be reached earlier.
/// \param DT Dominator tree used to find a common dominator.
/// \param PDT Post-dominator tree used to order the blocks.
/// \return True if \p ThisBlock is reached after \p OtherBlock in the CFG.
LLVM_ABI bool nonStrictlyPostDominate(const BasicBlock *ThisBlock,
                                      const BasicBlock *OtherBlock,
                                      const DominatorTree *DT,
                                      const PostDominatorTree *PDT);

/// Return true if \p I0 is reached before \p I1 in the control flow.
///
/// \param I0 Instruction that may be reached first.
/// \param I1 Instruction that may be reached second.
/// \param DT Dominator tree used to order instructions in the same block.
/// \param PDT Post-dominator tree used across different blocks.
/// \return True if \p I0 is reached before \p I1 in the control flow.
LLVM_ABI bool isReachedBefore(const Instruction *I0, const Instruction *I1,
                              const DominatorTree *DT,
                              const PostDominatorTree *PDT);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H
