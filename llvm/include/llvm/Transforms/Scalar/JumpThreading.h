//===- JumpThreading.h - thread control through conditional BBs -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// See the comments on JumpThreadingPass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H
#define LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <utility>

namespace llvm {

class AAResults;
class BasicBlock;
class BinaryOperator;
class CondBrInst;
class CmpInst;
class Constant;
class Function;
class Instruction;
class IntrinsicInst;
class LazyValueInfo;
class LoadInst;
class PHINode;
class SelectInst;
class SwitchInst;
class TargetLibraryInfo;
class TargetTransformInfo;
class Value;

/// A private "module" namespace for types and utilities used by
/// JumpThreading.
/// These are implementation details and should not be used by clients.
namespace jumpthreading {

// These are at global scope so static functions can use them too.
/// Non-owning view of constant values known along predecessor edges.
using PredValueInfo = SmallVectorImpl<std::pair<Constant *, BasicBlock *>>;
/// Owning list of constant values known along predecessor edges.
using PredValueInfoTy = SmallVector<std::pair<Constant *, BasicBlock *>, 8>;

/// Kind of constant sought when analyzing values in predecessors.
enum ConstantPreference {
  /// Prefer an integer constant (including undef).
  WantInteger,
  /// Prefer a block-address constant (including undef).
  WantBlockAddress
};

} // end namespace jumpthreading

/// Pass that performs jump threading across multi-predecessor blocks.
///
/// This pass performs 'jump threading', which looks at blocks that have
/// multiple predecessors and multiple successors.  If one or more of the
/// predecessors of the block can be proven to always jump to one of the
/// successors, we forward the edge from the predecessor to the successor by
/// duplicating the contents of this block.
///
/// An example of when this can occur is code like this:
///
///   if () { ...
///     X = 4;
///   }
///   if (X < 3) {
///
/// In this case, the unconditional branch at the end of the first if can be
/// revectored to the false side of the second if.
class JumpThreadingPass : public OptionalPassInfoMixin<JumpThreadingPass> {
  Function *F = nullptr;
  FunctionAnalysisManager *FAM = nullptr;
  TargetLibraryInfo *TLI = nullptr;
  TargetTransformInfo *TTI = nullptr;
  LazyValueInfo *LVI = nullptr;
  AAResults *AA = nullptr;
  std::unique_ptr<DomTreeUpdater> DTU;
  BlockFrequencyInfo *BFI = nullptr;
  BranchProbabilityInfo *BPI = nullptr;
  bool ChangedSinceLastAnalysisUpdate = false;
  bool HasGuards = false;
#ifndef LLVM_ENABLE_ABI_BREAKING_CHECKS
  SmallSet<AssertingVH<const BasicBlock>, 16> LoopHeaders;
#else
  SmallPtrSet<const BasicBlock *, 16> LoopHeaders;
#endif

  // JumpThreading must not processes blocks unreachable from entry. It's a
  // waste of compute time and can potentially lead to hangs.
  SmallPtrSet<BasicBlock *, 16> Unreachable;

  unsigned BBDupThreshold;
  unsigned DefaultBBDupThreshold;

public:
  /// Construct a jump-threading pass with an optional duplication threshold.
  /// @param T Maximum cost of instructions to duplicate when threading; -1
  /// uses the default threshold.
  LLVM_ABI JumpThreadingPass(int T = -1);

  /// Run jump threading using explicitly supplied analyses (legacy PM glue).
  /// @param F Function to optimize.
  /// @param FAM Function analysis manager, or nullptr.
  /// @param TLI Target library info.
  /// @param TTI Target transform info.
  /// @param LVI Lazy value info used to prove constant conditions.
  /// @param AA Alias analysis results.
  /// @param DTU Dominator-tree updater used while rewriting the CFG.
  /// @param BFI Optional block-frequency info, or nullptr.
  /// @param BPI Optional branch-probability info, or nullptr.
  /// @return True if the function was changed.
  LLVM_ABI bool runImpl(Function &F, FunctionAnalysisManager *FAM,
                        TargetLibraryInfo *TLI, TargetTransformInfo *TTI,
                        LazyValueInfo *LVI, AAResults *AA,
                        std::unique_ptr<DomTreeUpdater> DTU,
                        BlockFrequencyInfo *BFI, BranchProbabilityInfo *BPI);

  /// Run jump threading over the function.
  /// @param F Function to optimize.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Return the dominator-tree updater used by this pass.
  /// @return Pointer to the DomTreeUpdater, or nullptr if unset.
  DomTreeUpdater *getDomTreeUpdater() const { return DTU.get(); }
  /// Identify approximate loop headers that must not be jump-threaded across.
  /// @param F Function whose CFG is scanned for backedge targets.
  LLVM_ABI void findLoopHeaders(Function &F);
  /// Process \p BB for profitable jump-threading opportunities.
  /// @param BB Basic block to analyze and possibly rewrite.
  /// @return True if the CFG or IR was changed.
  LLVM_ABI bool processBlock(BasicBlock *BB);
  /// Merge \p BB into its sole predecessor when safe and profitable.
  /// @param BB Basic block that may have a single predecessor.
  /// @return True if \p BB was merged into its predecessor.
  LLVM_ABI bool maybeMergeBasicBlockIntoOnlyPred(BasicBlock *BB);
  /// Update SSA after cloning instructions from \p BB into \p NewBB.
  /// @param BB Original block whose values may need PHI updates.
  /// @param NewBB Block containing the cloned instructions.
  /// @param ValueMapping Map from original values in \p BB to clones in
  /// \p NewBB.
  LLVM_ABI void updateSSA(BasicBlock *BB, BasicBlock *NewBB,
                          ValueToValueMapTy &ValueMapping);
  /// Clone instructions in [\p BI, \p BE) into \p NewBB for predecessor
  /// \p PredBB.
  /// @param ValueMapping Map filled with original-to-clone value mappings.
  /// @param BI Start of the instruction range to clone.
  /// @param BE End of the instruction range to clone.
  /// @param NewBB Destination block for the clones.
  /// @param PredBB Predecessor whose PHI incoming values should be cloned.
  LLVM_ABI void cloneInstructions(ValueToValueMapTy &ValueMapping,
                                  BasicBlock::iterator BI,
                                  BasicBlock::iterator BE, BasicBlock *NewBB,
                                  BasicBlock *PredBB);
  /// Thread an edge from predecessors of \p BB to \p SuccBB when profitable.
  /// @param BB Block to thread across.
  /// @param PredBBs Predecessors that should be factored and redirected.
  /// @param SuccBB Successor that those predecessors should jump to.
  /// @return True if the edge was threaded.
  LLVM_ABI bool tryThreadEdge(BasicBlock *BB,
                              const SmallVectorImpl<BasicBlock *> &PredBBs,
                              BasicBlock *SuccBB);
  /// Factor \p PredBBs and thread the resulting edge across \p BB to \p SuccBB.
  /// @param BB Block to thread across.
  /// @param PredBBs Predecessors that should be factored and redirected.
  /// @param SuccBB Successor that those predecessors should jump to.
  LLVM_ABI void threadEdge(BasicBlock *BB,
                           const SmallVectorImpl<BasicBlock *> &PredBBs,
                           BasicBlock *SuccBB);
  /// Duplicate a PHI-conditioned branch from \p BB into selected predecessors.
  /// @param BB Block ending in a conditional branch on a PHI (or freeze PHI).
  /// @param PredBBs Predecessors that should receive a copy of \p BB.
  /// @return True if duplication succeeded.
  LLVM_ABI bool duplicateCondBranchOnPHIIntoPred(
      BasicBlock *BB, const SmallVectorImpl<BasicBlock *> &PredBBs);

  /// Recursively compute constant values of \p V known in predecessors of \p BB.
  /// @param V Value to analyze.
  /// @param BB Block whose predecessors are queried.
  /// @param Result Filled with (constant, predecessor) pairs.
  /// @param Preference Kind of constant to accept.
  /// @param RecursionSet Tracks values already visited to avoid cycles.
  /// @param CxtI Optional context instruction for LazyValueInfo queries.
  /// @return True if any known constant values were found.
  LLVM_ABI bool computeValueKnownInPredecessorsImpl(
      Value *V, BasicBlock *BB, jumpthreading::PredValueInfo &Result,
      jumpthreading::ConstantPreference Preference,
      SmallPtrSet<Value *, 4> &RecursionSet, Instruction *CxtI = nullptr);
  /// Compute constant values of \p V known in predecessors of \p BB.
  /// @param V Value to analyze.
  /// @param BB Block whose predecessors are queried.
  /// @param Result Filled with (constant, predecessor) pairs.
  /// @param Preference Kind of constant to accept.
  /// @param CxtI Optional context instruction for LazyValueInfo queries.
  /// @return True if any known constant values were found.
  bool
  computeValueKnownInPredecessors(Value *V, BasicBlock *BB,
                                  jumpthreading::PredValueInfo &Result,
                                  jumpthreading::ConstantPreference Preference,
                                  Instruction *CxtI = nullptr) {
    SmallPtrSet<Value *, 4> RecursionSet;
    return computeValueKnownInPredecessorsImpl(V, BB, Result, Preference,
                                               RecursionSet, CxtI);
  }

  /// Evaluate \p cond along the edge from \p PredPredBB through \p BB's
  /// predecessor.
  /// @param BB Block with a single predecessor that is being threaded through.
  /// @param PredPredBB Grand-predecessor feeding that sole predecessor.
  /// @param cond Value to evaluate on that path.
  /// @param DL Data layout used for constant folding.
  /// @return The folded constant, or nullptr if unknown.
  LLVM_ABI Constant *evaluateOnPredecessorEdge(BasicBlock *BB,
                                               BasicBlock *PredPredBB,
                                               Value *cond,
                                               const DataLayout &DL);
  /// Attempt to thread through \p BB and its sole predecessor.
  /// @param BB Block that may be threaded through together with its predecessor.
  /// @param Cond Branch condition of \p BB.
  /// @return True if threading through both blocks succeeded.
  LLVM_ABI bool maybethreadThroughTwoBasicBlocks(BasicBlock *BB, Value *Cond);
  /// Thread the edge PredPredBB -> PredBB -> BB -> SuccBB through two blocks.
  /// @param PredPredBB Predecessor of \p PredBB that should be redirected.
  /// @param PredBB Intermediate block that is duplicated and threaded through.
  /// @param BB Second block that is threaded through.
  /// @param SuccBB Final successor after threading.
  LLVM_ABI void threadThroughTwoBasicBlocks(BasicBlock *PredPredBB,
                                            BasicBlock *PredBB, BasicBlock *BB,
                                            BasicBlock *SuccBB);
  /// Process edges where \p Cond is a known constant in some predecessors.
  /// @param Cond Condition controlling the terminator of \p BB.
  /// @param BB Block whose outgoing edges may be threaded.
  /// @param Preference Kind of constant to look for.
  /// @param CxtI Optional context instruction for LazyValueInfo queries.
  /// @return True if any threading transformation was applied.
  LLVM_ABI bool
  processThreadableEdges(Value *Cond, BasicBlock *BB,
                         jumpthreading::ConstantPreference Preference,
                         Instruction *CxtI = nullptr);

  /// Simplify a conditional branch on a PHI when it cannot be fully threaded.
  /// @param PN PHI (or freeze of a PHI) used as the branch condition.
  /// @return True if a simplification was applied.
  LLVM_ABI bool processBranchOnPHI(PHINode *PN);
  /// Simplify a conditional branch on an XOR when it cannot be fully threaded.
  /// @param BO XOR feeding the branch condition.
  /// @return True if a simplification was applied.
  LLVM_ABI bool processBranchOnXOR(BinaryOperator *BO);
  /// Fold \p BB's branch when a predecessor implies its condition.
  /// @param BB Block whose terminator may be simplified by implication.
  /// @return True if the condition or branch was simplified.
  LLVM_ABI bool processImpliedCondition(BasicBlock *BB);

  /// Eliminate an obviously partially redundant load by inserting a PHI.
  /// @param LI Load that may be partially redundant across predecessors.
  /// @return True if the load was replaced.
  LLVM_ABI bool simplifyPartiallyRedundantLoad(LoadInst *LI);
  /// Expand a select in \p Pred into a compare-and-branch for threading.
  /// @param Pred Predecessor containing the select and an unconditional branch.
  /// @param BB Successor that uses the select through a PHI.
  /// @param SI Select instruction to unfold.
  /// @param SIUse PHI in \p BB that uses \p SI.
  /// @param Idx Incoming-value index of \p SI in \p SIUse.
  LLVM_ABI void unfoldSelectInstr(BasicBlock *Pred, BasicBlock *BB,
                                  SelectInst *SI, PHINode *SIUse, unsigned Idx);

  /// Unfold a predecessor select feeding a compare-branch in \p BB.
  /// @param CondCmp Compare that uses a PHI fed by a select in a predecessor.
  /// @param BB Block containing \p CondCmp and the conditional branch.
  /// @return True if a select was unfolded.
  LLVM_ABI bool tryToUnfoldSelect(CmpInst *CondCmp, BasicBlock *BB);
  /// Unfold a predecessor select feeding a switch condition in \p BB.
  /// @param SI Switch whose condition is a PHI fed by a select.
  /// @param BB Block containing the switch.
  /// @return True if a select was unfolded.
  LLVM_ABI bool tryToUnfoldSelect(SwitchInst *SI, BasicBlock *BB);
  /// Unfold a select in \p BB that is controlled by a PHI (or PHI/cmp).
  /// @param BB Block containing the PHI/select pattern to expand.
  /// @return True if a select was unfolded.
  LLVM_ABI bool tryToUnfoldSelectInCurrBB(BasicBlock *BB);

  /// Propagate llvm.experimental.guard calls in \p BB into predecessors.
  /// @param BB Block that may contain guards eligible for threading.
  /// @return True if any guard was threaded.
  LLVM_ABI bool processGuards(BasicBlock *BB);
  /// Thread \p Guard from a diamond merge block into a branch implied by \p BI.
  /// @param BB Merge block containing the guard.
  /// @param Guard llvm.experimental.guard intrinsic to propagate.
  /// @param BI Conditional branch at the top of the diamond.
  /// @return True if the guard was threaded into a predecessor.
  LLVM_ABI bool threadGuard(BasicBlock *BB, IntrinsicInst *Guard,
                            CondBrInst *BI);

private:
  BasicBlock *splitBlockPreds(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                              const char *Suffix);
  void updateBlockFreqAndEdgeWeight(BasicBlock *PredBB, BasicBlock *BB,
                                    BasicBlock *NewBB, BasicBlock *SuccBB,
                                    BlockFrequencyInfo *BFI,
                                    BranchProbabilityInfo *BPI,
                                    bool HasProfile);
  /// Check if the block has profile metadata for its outgoing edges.
  bool doesBlockHaveProfileData(BasicBlock *BB);

  /// Returns analysis preserved by the pass.
  PreservedAnalyses getPreservedAnalysis() const;

  /// Helper function to run "external" analysis in the middle of JumpThreading.
  /// It takes care of updating/invalidating other existing analysis
  /// before/after  running the "external" one.
  template <typename AnalysisT>
  typename AnalysisT::Result *runExternalAnalysis();

  /// Returns an existing instance of BPI if any, otherwise nullptr. By
  /// "existing" we mean either cached result provided by FunctionAnalysisManger
  /// or created by preceding call to 'getOrCreateBPI'.
  BranchProbabilityInfo *getBPI();

  /// Returns an existing instance of BFI if any, otherwise nullptr. By
  /// "existing" we mean either cached result provided by FunctionAnalysisManger
  /// or created by preceding call to 'getOrCreateBFI'.
  BlockFrequencyInfo *getBFI();

  /// Returns an existing instance of BPI if any, otherwise:
  ///   if 'HasProfile' is true creates new instance through
  ///   FunctionAnalysisManager, otherwise nullptr.
  BranchProbabilityInfo *getOrCreateBPI(bool Force = false);

  /// Returns an existing instance of BFI if any, otherwise:
  ///   if 'HasProfile' is true creates new instance through
  ///   FunctionAnalysisManager, otherwise nullptr.
  BlockFrequencyInfo *getOrCreateBFI(bool Force = false);

  // Internal overload of evaluateOnPredecessorEdge().
  Constant *evaluateOnPredecessorEdge(BasicBlock *BB, BasicBlock *PredPredBB,
                                      Value *cond, const DataLayout &DL,
                                      SmallPtrSet<Value *, 8> &Visited);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H
