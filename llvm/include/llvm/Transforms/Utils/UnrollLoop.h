//===- llvm/Transforms/Utils/UnrollLoop.h - Unrolling utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some loop unrolling utilities. It does not define any
// actual pass or policy, but provides a single function to perform loop
// unrolling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H
#define LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {

class AssumptionCache;
class AAResults;
class BasicBlock;
class BlockFrequencyInfo;
class DependenceInfo;
class DominatorTree;
class Loop;
class LoopInfo;
class MDNode;
class ProfileSummaryInfo;
class OptimizationRemarkEmitter;
class ScalarEvolution;
class StringRef;
class Value;

/// Map from an original loop to the corresponding newly created loop clone.
using NewLoopsMap = SmallDenseMap<const Loop *, Loop *, 4>;

/// @{
/// Metadata attribute names
const char *const LLVMLoopUnrollFollowupAll = "llvm.loop.unroll.followup_all";
/// Metadata attribute name for follow-up attributes on the unrolled loop.
const char *const LLVMLoopUnrollFollowupUnrolled =
    "llvm.loop.unroll.followup_unrolled";
/// Metadata attribute name for follow-up attributes on the remainder loop.
const char *const LLVMLoopUnrollFollowupRemainder =
    "llvm.loop.unroll.followup_remainder";
/// @}

/// Add a cloned basic block to LoopInfo and record any newly created loop.
///
/// Adds \p ClonedBB to LoopInfo, creates a new loop for \p ClonedBB if
/// necessary, and adds a mapping from the original loop to the new loop in
/// \p NewLoops. Returns nullptr if no new loop was created, otherwise a
/// pointer to the original loop that \p OriginalBB was part of.
///
/// \param OriginalBB Original basic block that was cloned.
/// \param ClonedBB Clone of \p OriginalBB to insert into LoopInfo.
/// \param LI LoopInfo to update with the cloned block and any new loops.
/// \param NewLoops Map from original loops to newly created cloned loops.
/// \return The original loop that \p OriginalBB belonged to if a new loop was
///         created, otherwise nullptr.
LLVM_ABI const Loop *addClonedBlockToLoopInfo(BasicBlock *OriginalBB,
                                              BasicBlock *ClonedBB,
                                              LoopInfo *LI,
                                              NewLoopsMap &NewLoops);

/// Represents the result of a \c UnrollLoop invocation.
enum class LoopUnrollResult {
  /// The loop was not modified.
  Unmodified,

  /// The loop was partially unrolled -- we still have a loop, but with a
  /// smaller trip count.  We may also have emitted epilogue loop if the loop
  /// had a non-constant trip count.
  PartiallyUnrolled,

  /// The loop was fully unrolled into straight-line code.  We no longer have
  /// any back-edges.
  FullyUnrolled
};

/// Options controlling how \c UnrollLoop transforms a loop.
struct UnrollLoopOptions {
  /// Factor by which to unroll the loop.
  unsigned Count;
  /// Force unrolling even when heuristics would otherwise refuse.
  bool Force;
  /// Allow inserting a prolog/epilog so the latch trip count is a multiple of
  /// \c Count when the trip count is not known at compile time.
  bool Runtime;
  /// Allow computing an expensive run-time trip count for runtime unrolling.
  bool AllowExpensiveTripCount;
  /// Unroll any remainder/epilogue loop created for a non-multiple trip count.
  bool UnrollRemainder;
  /// Forget all SCEV expressions for the loop instead of selectively
  /// invalidating them after unrolling.
  bool ForgetAllSCEV;
  /// Optional convergent "heart" instruction that constrains unrolling.
  const Instruction *Heart = nullptr;
  /// Budget for SCEV expansion when materializing the run-time trip count.
  unsigned SCEVExpansionBudget;
  /// Allow runtime unrolling of loops that have multiple exit blocks.
  bool RuntimeUnrollMultiExit = false;
  /// Parallelize supported reductions by adding additional accumulators.
  bool AddAdditionalAccumulators = false;
};

/// Unroll loop \p L by the factor and policies in \p ULO.
///
/// The loop must be in LCSSA form. Unrolling can only fail when the loop's
/// latch block is not terminated by a conditional branch instruction. However,
/// if the trip count (and multiple) are not known, loop unrolling will mostly
/// produce more code that is no faster.
///
/// If \c ULO.Runtime is true then UnrollLoop will try to insert a prologue or
/// epilogue that ensures the latch has a trip multiple of \c ULO.Count.
/// UnrollLoop will not runtime-unroll the loop if computing the run-time trip
/// count will be expensive and \c ULO.AllowExpensiveTripCount is false.
///
/// The LoopInfo Analysis that is passed will be kept consistent. This utility
/// preserves LoopInfo. It will also preserve ScalarEvolution and DominatorTree
/// if they are non-null.
///
/// \param L Loop to unroll.
/// \param ULO Unrolling options including count and runtime behavior.
/// \param LI LoopInfo to keep consistent with the transformed CFG.
/// \param SE Optional ScalarEvolution analysis to preserve and use.
/// \param DT Dominator tree required for the transformation.
/// \param AC Optional assumption cache used by simplifications.
/// \param TTI Optional target transform info for cost-aware decisions.
/// \param ORE Optional remark emitter for optimization diagnostics.
/// \param PreserveLCSSA Whether to preserve LCSSA form after unrolling.
/// \param RemainderLoop If non-null, receives the remainder loop when one is
///        created and the loop is not fully unrolled.
/// \param AA Optional alias analysis used by post-unroll simplifications.
/// \return Whether the loop was unmodified, partially unrolled, or fully
///         unrolled.
LLVM_ABI LoopUnrollResult UnrollLoop(Loop *L, UnrollLoopOptions ULO,
                                     LoopInfo *LI, ScalarEvolution *SE,
                                     DominatorTree *DT, AssumptionCache *AC,
                                     const llvm::TargetTransformInfo *TTI,
                                     OptimizationRemarkEmitter *ORE,
                                     bool PreserveLCSSA,
                                     Loop **RemainderLoop = nullptr,
                                     AAResults *AA = nullptr);

/// Insert a runtime prolog or epilog so \p L can be unrolled by \p Count.
///
/// Ensures the latch trip count is a multiple of \p Count when the trip count
/// is not known statically. May create and optionally unroll a remainder loop.
///
/// \param L Loop to prepare for runtime unrolling.
/// \param Count Unroll factor that the remainder logic must satisfy.
/// \param AllowExpensiveTripCount Allow costly run-time trip-count computation.
/// \param UseEpilogRemainder Prefer an epilog remainder over a prolog.
/// \param UnrollRemainder Unroll the generated remainder loop when profitable.
/// \param ForgetAllSCEV Forget all SCEV expressions for the loop after rewrite.
/// \param LI LoopInfo to keep consistent.
/// \param SE ScalarEvolution used to compute the trip count.
/// \param DT Dominator tree to update.
/// \param AC Assumption cache used by subsequent simplifications.
/// \param TTI Target transform info for cost checks.
/// \param PreserveLCSSA Whether to preserve LCSSA form.
/// \param SCEVExpansionBudget Budget for expanding the trip-count SCEV.
/// \param RuntimeUnrollMultiExit Allow runtime unrolling with multiple exits.
/// \param ResultLoop If non-null, receives the remainder loop when created.
/// \param OriginalTripCount Optional original trip count used for probabilities.
/// \param OriginalLoopProb Optional original loop-latch branch probability.
/// \return True if the remainder transformation succeeded.
LLVM_ABI bool UnrollRuntimeLoopRemainder(
    Loop *L, unsigned Count, bool AllowExpensiveTripCount,
    bool UseEpilogRemainder, bool UnrollRemainder, bool ForgetAllSCEV,
    LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT, AssumptionCache *AC,
    const TargetTransformInfo *TTI, bool PreserveLCSSA,
    unsigned SCEVExpansionBudget, bool RuntimeUnrollMultiExit,
    Loop **ResultLoop = nullptr,
    std::optional<unsigned> OriginalTripCount = std::nullopt,
    BranchProbability OriginalLoopProb = BranchProbability::getUnknown());

/// Unroll the outer loop and jam its iterations with the inner loop.
///
/// \c isSafeToUnrollAndJam should be used prior to calling this to make sure
/// the unrolling will be valid. Checking profitability is also advisable.
///
/// \param L Outer loop to unroll-and-jam (must contain exactly one inner loop).
/// \param Count Unroll factor for the outer loop.
/// \param TripCount Known trip count of the outer loop, or 0 if unknown.
/// \param TripMultiple Greatest known multiple of the outer-loop trip count.
/// \param UnrollRemainder Unroll any epilogue created for a non-multiple count.
/// \param LI LoopInfo to keep consistent.
/// \param SE ScalarEvolution analysis to update.
/// \param DT Dominator tree to update.
/// \param AC Assumption cache used by simplifications.
/// \param TTI Target transform info for cost-aware decisions.
/// \param ORE Remark emitter for optimization diagnostics.
/// \param EpilogueLoop If non-null, receives the epilogue loop when one is
///        created and not fully unrolled.
/// \return Whether the loop was unmodified, partially unrolled, or fully
///         unrolled.
LLVM_ABI LoopUnrollResult UnrollAndJamLoop(
    Loop *L, unsigned Count, unsigned TripCount, unsigned TripMultiple,
    bool UnrollRemainder, LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT,
    AssumptionCache *AC, const TargetTransformInfo *TTI,
    OptimizationRemarkEmitter *ORE, Loop **EpilogueLoop = nullptr);

/// Return true if it is safe to unroll-and-jam loop \p L.
///
/// \param L Candidate outer loop to check.
/// \param SE ScalarEvolution used for dependence and trip-count queries.
/// \param DT Dominator tree used to validate control flow.
/// \param DI Dependence info used to prove independence of jammed iterations.
/// \param LI LoopInfo describing the loop nest.
/// \return True if it is safe to unroll-and-jam \p L.
LLVM_ABI bool isSafeToUnrollAndJam(Loop *L, ScalarEvolution &SE,
                                   DominatorTree &DT, DependenceInfo &DI,
                                   LoopInfo &LI);

/// Clean up and simplify a loop after unrolling.
///
/// Useful to simplify induction variables in the new loop and to run a quick
/// simplify/DCE pass over the affected instructions.
///
/// \param L Loop that was unrolled (or its remainder).
/// \param SimplifyIVs Whether to simplify induction variables in \p L.
/// \param LI LoopInfo used by IV simplification.
/// \param SE ScalarEvolution used when simplifying IVs.
/// \param DT Dominator tree required for simplifications.
/// \param AC Assumption cache used by dead-code cleanup.
/// \param TTI Target transform info used by IV simplification.
/// \param Blocks Blocks whose instructions should be considered for cleanup.
/// \param AA Optional alias analysis enabling load CSE after unrolling.
LLVM_ABI void simplifyLoopAfterUnroll(Loop *L, bool SimplifyIVs, LoopInfo *LI,
                                      ScalarEvolution *SE, DominatorTree *DT,
                                      AssumptionCache *AC,
                                      const TargetTransformInfo *TTI,
                                      ArrayRef<BasicBlock *> Blocks,
                                      AAResults *AA = nullptr);

/// Return the named unroll hint metadata operand from loop id \p LoopID.
///
/// For example, looks up \c "llvm.loop.unroll.count". Returns nullptr if no
/// such metadata node exists.
///
/// \param LoopID Loop id metadata node whose operands are searched.
/// \param Name Metadata string name of the unroll hint to find.
/// \return The matching metadata node, or nullptr if none exists.
LLVM_ABI MDNode *GetUnrollMetadata(MDNode *LoopID, StringRef Name);

/// Return the named unroll hint metadata node attached to loop \p L.
///
/// For example, looks up \c "llvm.loop.unroll.count". Returns nullptr if no
/// such metadata node exists.
///
/// \param L Loop whose loop-id metadata is searched.
/// \param Name Metadata string name of the unroll hint to find.
/// \return The matching metadata node, or nullptr if none exists.
LLVM_ABI MDNode *getUnrollMetadataForLoop(const Loop *L, StringRef Name);

/// Unroll pragma, metadata, and command-line overrides for a loop.
struct UnrollPragmaInfo {
  /// Collect pragma and user unroll directives that apply to loop \p L.
  ///
  /// \param L Loop whose unroll metadata and user overrides are inspected.
  LLVM_ABI UnrollPragmaInfo(const Loop *L);
  /// True if the user provided an explicit unroll count on the command line.
  const bool UserUnrollCount;
  /// True if the loop has an \c llvm.loop.unroll.full pragma.
  const bool PragmaFullUnroll;
  /// Unroll count from \c llvm.loop.unroll.count, or 0 if absent.
  const unsigned PragmaCount;
  /// True if the loop has an \c llvm.loop.unroll.enable pragma.
  const bool PragmaEnableUnroll;
  /// True if the loop has an \c llvm.loop.unroll.runtime.disable pragma.
  const bool PragmaRuntimeUnrollDisable;
  /// True if any explicit unroll directive or user count applies to the loop.
  const bool ExplicitUnroll;
};

/// Gather unrolling preferences from defaults, flags, TTI, and user overrides.
///
/// \param L Loop for which preferences are gathered.
/// \param SE ScalarEvolution used by target preference callbacks.
/// \param TTI Target transform info providing target-specific overrides.
/// \param BFI Optional block-frequency info for size-optimization decisions.
/// \param PSI Optional profile-summary info for size-optimization decisions.
/// \param ORE Remark emitter passed to target preference callbacks.
/// \param OptLevel Current optimization level used to select defaults.
/// \param UserThreshold Optional user override for the unroll threshold.
/// \param UserAllowPartial Optional user override for partial unrolling.
/// \param UserRuntime Optional user override for runtime unrolling.
/// \param UserUpperBound Optional user override for upper-bound unrolling.
/// \param UserFullUnrollMaxCount Optional user cap on full-unroll count.
/// \return Combined unrolling preferences for \p L.
LLVM_ABI TargetTransformInfo::UnrollingPreferences gatherUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, const TargetTransformInfo &TTI,
    BlockFrequencyInfo *BFI, ProfileSummaryInfo *PSI,
    llvm::OptimizationRemarkEmitter &ORE, int OptLevel,
    std::optional<unsigned> UserThreshold, std::optional<bool> UserAllowPartial,
    std::optional<bool> UserRuntime, std::optional<bool> UserUpperBound,
    std::optional<unsigned> UserFullUnrollMaxCount);

/// Produce an estimate of the unrolled cost of the specified loop.
///
/// This is used to a) produce a cost estimate for partial unrolling and b) to cheaply estimate cost for full unrolling when we don't want to symbolically evaluate all iterations.
class UnrollCostEstimator {
  InstructionCost LoopSize;
  bool NotDuplicatable;

public:
  /// Number of calls that look like profitable inlining candidates.
  unsigned NumInlineCandidates;
  /// Convergence kind of operations found in the loop body.
  ConvergenceKind Convergence;
  /// True if convergence still permits runtime unrolling with a remainder.
  bool ConvergenceAllowsRuntime;

  /// Estimate the rolled size and convergence properties of loop \p L.
  ///
  /// \param L Loop whose body cost is analyzed.
  /// \param TTI Target transform info used by code-size metrics.
  /// \param EphValues Ephemeral values excluded from the size estimate.
  /// \param BEInsns Estimated back-edge instruction count used as a size floor.
  /// \param PrepareForLTO If true, consider calls as inline candidates and
  /// defer unrolling so that LTO post-link inlining can consider them first.
  /// \param TripCountIsUniform If true, all threads in a convergent execution
  /// agree on the trip count, so runtime unrolling with a remainder is safe
  /// even for loops with uncontrolled convergent operations.
  LLVM_ABI UnrollCostEstimator(const Loop *L, const TargetTransformInfo &TTI,
                               const SmallPtrSetImpl<const Value *> &EphValues,
                               unsigned BEInsns, bool PrepareForLTO = false,
                               bool TripCountIsUniform = false);

  /// Whether it is legal to unroll this loop. If \p ORE and \p L are provided,
  /// emit an optimization remark on failure.
  ///
  /// \param ORE Optional remark emitter used to report why unrolling failed.
  /// \param L Optional loop used as the remark location when \p ORE is set.
  /// \return True if this loop is legal to unroll.
  LLVM_ABI bool canUnroll(OptimizationRemarkEmitter *ORE = nullptr,
                          const Loop *L = nullptr) const;

  /// Return the estimated size of the loop before unrolling.
  ///
  /// \return Estimated size of the rolled loop body.
  uint64_t getRolledLoopSize() const { return LoopSize.getValue(); }

  /// Returns loop size estimation for an unrolled loop with the given unroll
  /// count and the unrolling configuration specified by UP.
  ///
  /// \param UP Unrolling preferences supplying the back-edge instruction count.
  /// \param Count Unroll factor used to scale the estimated body size.
  /// \return Estimated size of the loop after unrolling by \p Count.
  LLVM_ABI uint64_t
  getUnrolledLoopSize(const TargetTransformInfo::UnrollingPreferences &UP,
                      unsigned Count) const;
};

/// Compute the unroll count for \p L from analysis, pragmas, and preferences.
///
/// Uses metadata and command-line options that are specific to the LoopUnroll
/// pass.
///
/// \param L Loop whose unroll count is being decided.
/// \param TTI Target transform info used for cost modeling.
/// \param DT Dominator tree used by peeling and unrolling heuristics.
/// \param LI LoopInfo describing the loop nest.
/// \param AC Assumption cache used by cost analysis.
/// \param SE ScalarEvolution providing trip-count information.
/// \param EphValues Ephemeral values excluded from size estimates.
/// \param ORE Remark emitter for unrolling diagnostics.
/// \param TripCount Exact trip count when known, otherwise 0.
/// \param MaxTripCount Maximum trip count when an exact count is unknown.
/// \param MaxOrZero True when the trip count is either \p MaxTripCount or zero.
/// \param TripMultiple Greatest known multiple of the trip count.
/// \param UCE Precomputed cost estimate for the rolled loop.
/// \param UP Unrolling preferences updated with the chosen policy.
/// \param PP Peeling preferences that may be adjusted alongside unrolling.
/// \return Chosen unroll factor for \p L.
LLVM_ABI unsigned
computeUnrollCount(Loop *L, const TargetTransformInfo &TTI, DominatorTree &DT,
                   LoopInfo *LI, AssumptionCache *AC, ScalarEvolution &SE,
                   const SmallPtrSetImpl<const Value *> &EphValues,
                   OptimizationRemarkEmitter *ORE, unsigned TripCount,
                   unsigned MaxTripCount, bool MaxOrZero, unsigned TripMultiple,
                   const UnrollCostEstimator &UCE,
                   TargetTransformInfo::UnrollingPreferences &UP,
                   TargetTransformInfo::PeelingPreferences &PP);

/// Return a recurrence descriptor if \p Phi can be parallelized when unrolling.
///
/// \param Phi Candidate reduction PHI in loop \p L.
/// \param L Loop containing \p Phi.
/// \param SE ScalarEvolution used to classify the reduction.
/// \return A descriptor for a supported parallelizable reduction, or
///         \c std::nullopt if \p Phi cannot be handled.
LLVM_ABI std::optional<RecurrenceDescriptor>
canParallelizeReductionWhenUnrolling(PHINode &Phi, Loop *L,
                                     ScalarEvolution *SE);
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_UNROLLLOOP_H
