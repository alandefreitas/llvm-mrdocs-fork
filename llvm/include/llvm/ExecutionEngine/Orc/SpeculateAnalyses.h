//===-- SpeculateAnalyses.h  --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains the Analyses and Result Interpretation to select likely functions
/// to Speculatively compile before they are called. [Purely Experimentation]
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H
#define LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace orc {

/// Base class providing shared helpers for speculation analyses.
class SpeculateQuery {
protected:
  /// Collect names of functions called directly from \p BB into \p CallesNames.
  /// @param BB Basic block to scan for direct calls and invokes.
  /// @param CallesNames Set receiving the names of directly called functions.
  LLVM_ABI void findCalles(const BasicBlock *BB,
                           DenseSet<StringRef> &CallesNames);
  /// Return true if every basic block in \p F has exactly one successor.
  /// @param F Function whose CFG shape is tested.
  /// @return True if \p F is a straight-line CFG; false otherwise.
  LLVM_ABI bool isStraightLine(const Function &F);

public:
  /// Optional map from a caller function name to the set of likely callee names.
  using ResultTy = std::optional<DenseMap<StringRef, DenseSet<StringRef>>>;
};

/// Speculative query that ranks callees by basic-block frequency.
///
/// Direct calls in high frequency basic blocks are extracted.
class BlockFreqQuery : public SpeculateQuery {
  size_t numBBToGet(size_t);

public:
  /// Find likely next executables based on IR block frequency.
  /// @param F Function to analyze for high-frequency call sites.
  /// @return Optional map from caller name to likely callee names, or nullopt.
  LLVM_ABI ResultTy operator()(Function &F);
};

/// Speculative query that walks a CFG sequence of hot basic blocks.
///
/// This query generates a sequence of basic blocks which follows the order of
/// execution. A handful of BB with higher block frequencies are taken, then
/// path to entry and end BB are discovered by traversing up & down the CFG.
class SequenceBBQuery : public SpeculateQuery {
  struct WalkDirection {
    bool Upward = true, Downward = true;
    // the block associated contain a call
    bool CallerBlock = false;
  };

public:
  /// Map from a basic block to the directions still open while walking the CFG.
  using VisitedBlocksInfoTy = DenseMap<const BasicBlock *, WalkDirection>;
  /// Ordered list of basic blocks discovered by a sequence query.
  using BlockListTy = SmallVector<const BasicBlock *, 8>;
  /// List of CFG back-edges as (source, destination) basic-block pairs.
  using BackEdgesInfoTy =
      SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8>;
  /// List of basic blocks paired with their block-frequency values.
  using BlockFreqInfoTy =
      SmallVector<std::pair<const BasicBlock *, uint64_t>, 8>;

private:
  std::size_t getHottestBlocks(std::size_t TotalBlocks);
  BlockListTy rearrangeBB(const Function &, const BlockListTy &);
  BlockListTy queryCFG(Function &, const BlockListTy &);
  void traverseToEntryBlock(const BasicBlock *, const BlockListTy &,
                            const BackEdgesInfoTy &,
                            const BranchProbabilityInfo *,
                            VisitedBlocksInfoTy &);
  void traverseToExitBlock(const BasicBlock *, const BlockListTy &,
                           const BackEdgesInfoTy &,
                           const BranchProbabilityInfo *,
                           VisitedBlocksInfoTy &);

public:
  /// Analyze \p F and return likely callee names along hot CFG sequences.
  /// @param F Function whose hot call paths are sequenced for speculation.
  /// @return Optional map from caller name to likely callee names, or nullopt.
  LLVM_ABI ResultTy operator()(Function &F);
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SPECULATEANALYSES_H
