//===- SSAUpdaterBulk.h - Unstructured SSA Update Tool ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SSAUpdaterBulk class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H
#define LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class PHINode;
template <typename T> class SmallVectorImpl;
class Type;
class Use;
class Value;
class DominatorTree;

/// Helper class for SSA formation on a set of values defined in multiple
/// blocks.
///
/// This is used when code duplication or another unstructured transformation
/// wants to rewrite a set of uses of one value with uses of a set of values.
/// The update is done only when RewriteAllUses is called, all other methods are
/// used for book-keeping. That helps to share some common computations between
/// updates of different uses (which is not the case when traditional SSAUpdater
/// is used).
class SSAUpdaterBulk {
  struct RewriteInfo {
    SmallVector<std::pair<BasicBlock *, Value *>, 4> Defines;
    SmallVector<Use *, 4> Uses;
    StringRef Name;
    Type *Ty;
    RewriteInfo() = default;
    RewriteInfo(StringRef &N, Type *T) : Name(N), Ty(T){};
  };
  SmallVector<RewriteInfo, 4> Rewrites;

  PredIteratorCache PredCache;

public:
  /// Construct an empty SSA updater.
  explicit SSAUpdaterBulk() = default;
  /// Deleted copy constructor; SSAUpdaterBulk is not copyable.
  ///
  /// \param Other Unused; copy construction is not allowed.
  SSAUpdaterBulk(const SSAUpdaterBulk &Other) = delete;
  /// Deleted copy assignment; SSAUpdaterBulk cannot be copy-assigned.
  ///
  /// \param Other Unused; copy assignment is not allowed.
  SSAUpdaterBulk &operator=(const SSAUpdaterBulk &Other) = delete;
  /// Destroy the SSA updater.
  ~SSAUpdaterBulk() = default;

  /// Add a new variable to the SSA rewriter.
  ///
  /// This needs to be called before AddAvailableValue or AddUse calls. The
  /// return value is the variable ID, which needs to be passed to
  /// AddAvailableValue and AddUse.
  ///
  /// \param Name Name used when creating PHI nodes for this variable.
  /// \param Ty Type of the values being rewritten for this variable.
  /// \returns Variable ID to pass to AddAvailableValue and AddUse.
  LLVM_ABI unsigned AddVariable(StringRef Name, Type *Ty);

  /// Indicate that a rewritten value is available in the specified block with
  /// the specified value.
  ///
  /// \param Var Variable ID returned by AddVariable.
  /// \param BB Block in which the rewritten value is available.
  /// \param V Value available in \p BB.
  LLVM_ABI void AddAvailableValue(unsigned Var, BasicBlock *BB, Value *V);

  /// Record a use of the symbolic value. This use will be updated with a
  /// rewritten value when RewriteAllUses is called.
  ///
  /// \param Var Variable ID returned by AddVariable.
  /// \param U Use to rewrite when RewriteAllUses is called.
  LLVM_ABI void AddUse(unsigned Var, Use *U);

  /// Perform all the necessary updates, including new PHI-nodes insertion and
  /// the requested uses update.
  ///
  /// The function requires dominator tree DT, which is used for computing
  /// locations for new phi-nodes insertions. If a nonnull pointer to a vector
  /// InsertedPHIs is passed, all the new phi-nodes will be added to this
  /// vector.
  ///
  /// \param DT Dominator tree used to place newly inserted PHI nodes.
  /// \param InsertedPHIs Optional list that receives newly inserted PHI nodes.
  LLVM_ABI void
  RewriteAllUses(DominatorTree *DT,
                 SmallVectorImpl<PHINode *> *InsertedPHIs = nullptr);

  /// Rewrite all uses and simplify the inserted PHI nodes.
  ///
  /// Use this method to preserve behavior when replacing SSAUpdater.
  ///
  /// \param DT Dominator tree used while rewriting uses.
  LLVM_ABI void RewriteAndOptimizeAllUses(DominatorTree &DT);
};

/// Eliminate newly inserted PHI nodes in \p BB that duplicate earlier ones.
///
/// New PHI nodes are those before \p FirstExistingPN in the block's PHI list;
/// existing ones start at that iterator. Identical new PHIs are first
/// deduplicated among themselves, then against existing PHIs.
///
/// \param BB Block whose PHI nodes are considered.
/// \param FirstExistingPN Iterator to the first PHI that already existed.
/// \returns True if any duplicate PHI was eliminated.
LLVM_ABI bool
EliminateNewDuplicatePHINodes(BasicBlock *BB,
                              BasicBlock::phi_iterator FirstExistingPN);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H
