//===- llvm/Transforms/Utils/LoopUtils.h - Loop utilities -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some loop transformation utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPUTILS_H
#define LLVM_TRANSFORMS_UTILS_LOOPUTILS_H

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {

template <typename T> class DomTreeNodeBase;
using DomTreeNode = DomTreeNodeBase<BasicBlock>;
class AssumptionCache;
class StringRef;
class AnalysisUsage;
class TargetTransformInfo;
class AAResults;
class BasicBlock;
class ICFLoopSafetyInfo;
class IRBuilderBase;
class Loop;
class LoopInfo;
class MemoryAccess;
class MemorySSA;
class MemorySSAUpdater;
class OptimizationRemarkEmitter;
struct PointerDiffInfo;
class PredIteratorCache;
class ScalarEvolution;
class SCEV;
class SCEVExpander;
class TargetLibraryInfo;
class LPPassManager;
class Instruction;
struct RuntimeCheckingPtrGroup;
/// A runtime memcheck made of a pair of grouped pointers.
typedef std::pair<const RuntimeCheckingPtrGroup *,
                  const RuntimeCheckingPtrGroup *>
    RuntimePointerCheck;

template <typename T, unsigned N> class SmallSetVector;
template <typename T, unsigned N> class SmallPriorityWorklist;

/// Insert a loop preheader for \p L when one does not already exist.
///
/// Updates analysis as needed. Returns the new preheader, or nullptr if a
/// preheader could not be created (for example when an indirect branch
/// prevents edge splitting).
///
/// @param L The loop that needs a preheader.
/// @param DT Dominator tree to update, or nullptr.
/// @param LI Loop info to update, or nullptr.
/// @param MSSAU MemorySSA updater, or nullptr.
/// @param PreserveLCSSA Whether to preserve LCSSA form when splitting.
/// @return The new preheader, or nullptr if one could not be created.
LLVM_ABI BasicBlock *InsertPreheaderForLoop(Loop *L, DominatorTree *DT,
                                            LoopInfo *LI,
                                            MemorySSAUpdater *MSSAU,
                                            bool PreserveLCSSA);

/// Ensure that all exit blocks of the loop are dedicated exits.
///
/// For any loop exit block with non-loop predecessors, we split the loop
/// predecessors to use a dedicated loop exit block. We update the dominator
/// tree and loop info if provided, and will preserve LCSSA if requested.
///
/// @param L The loop whose exits should be made dedicated.
/// @param DT Dominator tree to update, or nullptr.
/// @param LI Loop info to update, or nullptr.
/// @param MSSAU MemorySSA updater, or nullptr.
/// @param PreserveLCSSA Whether to preserve LCSSA form when splitting.
/// @return True if any exit blocks were made dedicated.
LLVM_ABI bool formDedicatedExitBlocks(Loop *L, DominatorTree *DT, LoopInfo *LI,
                                      MemorySSAUpdater *MSSAU,
                                      bool PreserveLCSSA);

/// Ensures LCSSA form for every instruction from the Worklist in the scope of
/// innermost containing loop.
///
/// For the given instruction which have uses outside of the loop, an LCSSA PHI
/// node is inserted and the uses outside the loop are rewritten to use this
/// node.
///
/// LoopInfo and DominatorTree are required and, since the routine makes no
/// changes to CFG, preserved.
///
/// Returns true if any modifications are made.
///
/// This function may introduce unused PHI nodes. If \p PHIsToRemove is not
/// nullptr, those are added to it (before removing, the caller has to check if
/// they still do not have any uses). Otherwise the PHIs are directly removed.
///
/// If \p InsertedPHIs is not nullptr, inserted phis will be added to this
/// vector.
///
/// @param Worklist Instructions that may need LCSSA PHI nodes.
/// @param DT Dominator tree for the function.
/// @param LI Loop info for the function.
/// @param SE Optional ScalarEvolution to preserve, or nullptr.
/// @param PHIsToRemove Optional list to collect unused PHIs for the caller.
/// @param InsertedPHIs Optional list to collect newly inserted PHIs.
/// @return True if any modifications were made.
LLVM_ABI bool
formLCSSAForInstructions(SmallVectorImpl<Instruction *> &Worklist,
                         const DominatorTree &DT, const LoopInfo &LI,
                         ScalarEvolution *SE,
                         SmallVectorImpl<PHINode *> *PHIsToRemove = nullptr,
                         SmallVectorImpl<PHINode *> *InsertedPHIs = nullptr);

/// Put loop into LCSSA form.
///
/// Looks at all instructions in the loop which have uses outside of the
/// current loop. For each, an LCSSA PHI node is inserted and the uses outside
/// the loop are rewritten to use this node. Sub-loops must be in LCSSA form
/// already.
///
/// LoopInfo and DominatorTree are required and preserved.
///
/// If ScalarEvolution is passed in, it will be preserved.
///
/// Returns true if any modifications are made to the loop.
///
/// @param L The loop to put into LCSSA form.
/// @param DT Dominator tree for the function.
/// @param LI Loop info for the function.
/// @param SE Optional ScalarEvolution to preserve, or nullptr.
/// @return True if any modifications were made to the loop.
LLVM_ABI bool formLCSSA(Loop &L, const DominatorTree &DT, const LoopInfo *LI,
                        ScalarEvolution *SE);

/// Put a loop nest into LCSSA form.
///
/// This recursively forms LCSSA for a loop nest.
///
/// LoopInfo and DominatorTree are required and preserved.
///
/// If ScalarEvolution is passed in, it will be preserved.
///
/// Returns true if any modifications are made to the loop.
///
/// @param L The outermost loop of the nest to put into LCSSA form.
/// @param DT Dominator tree for the function.
/// @param LI Loop info for the function.
/// @param SE Optional ScalarEvolution to preserve, or nullptr.
/// @return True if any modifications were made to the loop.
LLVM_ABI bool formLCSSARecursively(Loop &L, const DominatorTree &DT,
                                   const LoopInfo *LI, ScalarEvolution *SE);

/// Flags controlling how much is checked when sinking or hoisting instructions.
///
/// The number of memory accesses in the loop (and whether there are too many)
/// is determined in the constructors when using MemorySSA.
class SinkAndHoistLICMFlags {
public:
  /// Construct flags with explicit MemorySSA optimization caps.
  ///
  /// @param LicmMssaOptCap Max clobbering-call queries before giving up.
  /// @param LicmMssaNoAccForPromotionCap Max memory accesses before promotion
  ///                                     is considered too expensive.
  /// @param IsSink True when these flags are used for sinking.
  /// @param L The loop being processed.
  /// @param MSSA MemorySSA for \p L.
  LLVM_ABI SinkAndHoistLICMFlags(unsigned LicmMssaOptCap,
                                 unsigned LicmMssaNoAccForPromotionCap,
                                 bool IsSink, Loop &L, MemorySSA &MSSA);
  /// Construct flags using the default MemorySSA optimization caps.
  ///
  /// @param IsSink True when these flags are used for sinking.
  /// @param L The loop being processed.
  /// @param MSSA MemorySSA for \p L.
  LLVM_ABI SinkAndHoistLICMFlags(bool IsSink, Loop &L, MemorySSA &MSSA);

  /// Set whether LICM is currently sinking rather than hoisting.
  ///
  /// @param B True for sinking, false for hoisting.
  void setIsSink(bool B) { IsSink = B; }
  /// Return true if LICM is currently sinking rather than hoisting.
  ///
  /// @return True if LICM is currently sinking rather than hoisting.
  bool getIsSink() { return IsSink; }
  /// Return true if the loop has too many memory accesses for promotion.
  ///
  /// @return True if the loop has too many memory accesses for promotion.
  bool tooManyMemoryAccesses() { return NoOfMemAccTooLarge; }
  /// Return true if clobbering-call queries exceeded the configured cap.
  ///
  /// @return True if clobbering-call queries exceeded the configured cap.
  bool tooManyClobberingCalls() { return LicmMssaOptCounter >= LicmMssaOptCap; }
  /// Record that another clobbering-call query was performed.
  void incrementClobberingCalls() { ++LicmMssaOptCounter; }

protected:
  /// True when the loop's memory-access count exceeds the promotion cap.
  bool NoOfMemAccTooLarge = false;
  /// Number of clobbering-call queries performed so far.
  unsigned LicmMssaOptCounter = 0;
  /// Maximum clobbering-call queries before optimization backs off.
  unsigned LicmMssaOptCap;
  /// Maximum memory accesses before scalar promotion is considered too costly.
  unsigned LicmMssaNoAccForPromotionCap;
  /// True when these flags apply to sinking rather than hoisting.
  bool IsSink;
};

/// Sink invariant instructions out of a loop region in reverse DFS order.
///
/// Walk the specified region of the CFG (defined by all blocks dominated by
/// the specified block, and that are in the current loop) in reverse depth
/// first order w.r.t the DominatorTree. This allows us to visit uses before
/// definitions, allowing us to sink a loop body in one pass without iteration.
/// Takes DomTreeNode, AAResults, LoopInfo, DominatorTree, TargetLibraryInfo,
/// Loop, AliasSet information for all instructions of the loop and loop safety
/// information as arguments. Diagnostics is emitted via \p ORE. It returns
/// changed status. \p CurLoop is a loop to do sinking on. \p OutermostLoop is
/// used only when this function is called by \p sinkRegionForLoopNest.
///
/// @param N Dominator-tree node defining the region to walk.
/// @param AA Alias analysis results.
/// @param LI Loop info for the function.
/// @param DT Dominator tree for the function.
/// @param TLI Target library info.
/// @param TTI Target transform info.
/// @param CurLoop Loop whose body may be sunk.
/// @param MSSAU MemorySSA updater for the transform.
/// @param SafetyInfo Loop safety information for faulting ops.
/// @param Flags Caps controlling MemorySSA query cost.
/// @param ORE Optional remark emitter for diagnostics.
/// @param OutermostLoop Outermost loop when called from nest sinking.
/// @return True if any instructions were sunk.
LLVM_ABI bool sinkRegion(DomTreeNode *N, AAResults *AA, LoopInfo *LI,
                         DominatorTree *DT, TargetLibraryInfo *TLI,
                         TargetTransformInfo *TTI, Loop *CurLoop,
                         MemorySSAUpdater &MSSAU, ICFLoopSafetyInfo *SafetyInfo,
                         SinkAndHoistLICMFlags &Flags,
                         OptimizationRemarkEmitter *ORE,
                         Loop *OutermostLoop = nullptr);

/// Sink invariant instructions for every loop in a nest, inner to outer.
///
/// Call sinkRegion on loops contained within the specified loop in order from
/// innermost to outermost.
///
/// @param N Dominator-tree node for the outermost loop header.
/// @param AA Alias analysis results.
/// @param LI Loop info for the function.
/// @param DT Dominator tree for the function.
/// @param TLI Target library info.
/// @param TTI Target transform info.
/// @param CurLoop Outermost loop of the nest to sink within.
/// @param MSSAU MemorySSA updater for the transform.
/// @param SafetyInfo Loop safety information for faulting ops.
/// @param Flags Caps controlling MemorySSA query cost.
/// @param ORE Optional remark emitter for diagnostics.
/// @return True if any instructions were sunk.
LLVM_ABI bool sinkRegionForLoopNest(DomTreeNode *N, AAResults *AA, LoopInfo *LI,
                                    DominatorTree *DT, TargetLibraryInfo *TLI,
                                    TargetTransformInfo *TTI, Loop *CurLoop,
                                    MemorySSAUpdater &MSSAU,
                                    ICFLoopSafetyInfo *SafetyInfo,
                                    SinkAndHoistLICMFlags &Flags,
                                    OptimizationRemarkEmitter *ORE);

/// Hoist invariant instructions into a loop preheader in DFS order.
///
/// Walk the specified region of the CFG (defined by all blocks dominated by
/// the specified block, and that are in the current loop) in depth first order
/// w.r.t the DominatorTree. This allows us to visit definitions before uses,
/// allowing us to hoist a loop body in one pass without iteration. Takes
/// DomTreeNode, AAResults, LoopInfo, DominatorTree, TargetLibraryInfo, Loop,
/// AliasSet information for all instructions of the loop and loop safety
/// information as arguments. Diagnostics is emitted via \p ORE. It returns
/// changed status. \p AllowSpeculation is whether values should be hoisted even
/// if they are not guaranteed to execute in the loop, but are safe to
/// speculatively execute.
///
/// @param N Dominator-tree node defining the region to walk.
/// @param AA Alias analysis results.
/// @param LI Loop info for the function.
/// @param DT Dominator tree for the function.
/// @param AC Assumption cache for the function.
/// @param TLI Target library info.
/// @param CurLoop Loop whose body may be hoisted.
/// @param MSSAU MemorySSA updater for the transform.
/// @param SE ScalarEvolution analysis, or nullptr.
/// @param SafetyInfo Loop safety information for faulting ops.
/// @param Flags Caps controlling MemorySSA query cost.
/// @param ORE Optional remark emitter for diagnostics.
/// @param LoopNestMode True when hoisting across a loop nest.
/// @param AllowSpeculation Whether to hoist values that may not always execute.
/// @return True if any instructions were hoisted.
LLVM_ABI bool hoistRegion(DomTreeNode *N, AAResults *AA, LoopInfo *LI,
                          DominatorTree *DT, AssumptionCache *AC,
                          TargetLibraryInfo *TLI, Loop *CurLoop,
                          MemorySSAUpdater &MSSAU, ScalarEvolution *SE,
                          ICFLoopSafetyInfo *SafetyInfo,
                          SinkAndHoistLICMFlags &Flags,
                          OptimizationRemarkEmitter *ORE, bool LoopNestMode,
                          bool AllowSpeculation);

/// Return true if removing exit test \p Cond would make induction variable
/// \p IV dead.
///
/// Conservatively returns false if analysis is insufficient.
///
/// @param IV Induction-variable PHI in the loop.
/// @param LatchBlock Latch block of the loop containing \p IV.
/// @param Cond Exit-test condition that would be removed.
/// @return True if removing \p Cond would make \p IV dead.
LLVM_ABI bool isAlmostDeadIV(PHINode *IV, BasicBlock *LatchBlock, Value *Cond);

/// Delete a loop that the caller has proven dead.
///
/// The caller of this function needs to guarantee that the loop is in fact
/// dead. The function requires a bunch of prerequisites to be present:
///   - The loop needs to be in LCSSA form
///   - The loop needs to have a Preheader
///   - A unique dedicated exit block must exist
///
/// This also updates the relevant analysis information in \p DT, \p SE, \p LI
/// and \p MSSA if pointers to those are provided. It also updates the loop PM
/// if an updater struct is provided.
///
/// @param L The dead loop to delete.
/// @param DT Dominator tree to update, or nullptr.
/// @param SE ScalarEvolution to update, or nullptr.
/// @param LI Loop info to update, or nullptr.
/// @param MSSA MemorySSA to update, or nullptr.
LLVM_ABI void deleteDeadLoop(Loop *L, DominatorTree *DT, ScalarEvolution *SE,
                             LoopInfo *LI, MemorySSA *MSSA = nullptr);

/// Remove the backedge of a top-level single-latch loop.
///
/// Handles loop nests and general loop structures subject to the precondition
/// that the loop has no parent loop and has a single latch block. Preserves all
/// listed analyses.
///
/// @param L The loop whose backedge should be removed.
/// @param DT Dominator tree to update.
/// @param SE ScalarEvolution to update.
/// @param LI Loop info to update.
/// @param MSSA MemorySSA to update, or nullptr.
LLVM_ABI void breakLoopBackedge(Loop *L, DominatorTree &DT, ScalarEvolution &SE,
                                LoopInfo &LI, MemorySSA *MSSA);

/// Promote must-alias loop memory accesses to scalars via load/store motion.
///
/// Try to promote memory values to scalars by sinking stores out of the loop
/// and moving loads to before the loop. We do this by looping over the stores
/// in the loop, looking for stores to Must pointers which are loop invariant.
/// It takes a set of must-alias values, Loop exit blocks vector, loop exit
/// blocks insertion point vector, PredIteratorCache, LoopInfo, DominatorTree,
/// Loop, AliasSet information for all instructions of the loop and loop safety
/// information as arguments. Diagnostics is emitted via \p ORE. It returns
/// changed status. \p AllowSpeculation is whether values should be hoisted even
/// if they are not guaranteed to execute in the loop, but are safe to
/// speculatively execute.
///
/// @param PointerMustAliases Must-alias pointer set considered for promotion.
/// @param ExitBlocks Loop exit blocks used for sunk stores.
/// @param InsertPts Insertion points in the corresponding exit blocks.
/// @param MSSAInsertPts MemorySSA insertion points for promoted accesses.
/// @param PIC Predecessor-iterator cache for the transform.
/// @param LI Loop info for the function.
/// @param DT Dominator tree for the function.
/// @param AC Assumption cache for the function.
/// @param TLI Target library info.
/// @param TTI Target transform info.
/// @param CurLoop Loop whose accesses may be promoted.
/// @param MSSAU MemorySSA updater for the transform.
/// @param SafetyInfo Loop safety information for faulting ops.
/// @param ORE Optional remark emitter for diagnostics.
/// @param AllowSpeculation Whether to hoist values that may not always execute.
/// @param HasReadsOutsideSet Whether the alias set has reads outside the set.
/// @return True if any accesses were promoted.
LLVM_ABI bool promoteLoopAccessesToScalars(
    const SmallSetVector<Value *, 8> &PointerMustAliases,
    SmallVectorImpl<BasicBlock *> &ExitBlocks,
    SmallVectorImpl<BasicBlock::iterator> &InsertPts,
    SmallVectorImpl<MemoryAccess *> &MSSAInsertPts, PredIteratorCache &PIC,
    LoopInfo *LI, DominatorTree *DT, AssumptionCache *AC,
    const TargetLibraryInfo *TLI, TargetTransformInfo *TTI, Loop *CurLoop,
    MemorySSAUpdater &MSSAU, ICFLoopSafetyInfo *SafetyInfo,
    OptimizationRemarkEmitter *ORE, bool AllowSpeculation,
    bool HasReadsOutsideSet);

/// Does a BFS from a given node to all of its children inside a given loop.
/// The returned vector of basic blocks includes the starting point.
///
/// @param DT Dominator tree used to walk children.
/// @param N Dominator-tree node to start the BFS from.
/// @param CurLoop Loop that bounds which children are collected.
/// @return Basic blocks reachable from \p N inside \p CurLoop, including the
///         start.
LLVM_ABI SmallVector<BasicBlock *, 16>
collectChildrenInLoop(DominatorTree *DT, DomTreeNode *N, const Loop *CurLoop);

/// Returns the instructions that use values defined in the loop.
///
/// @param L The loop whose externally used definitions are collected.
/// @return Instructions that use values defined in the loop.
LLVM_ABI SmallVector<Instruction *, 8> findDefsUsedOutsideOfLoop(Loop *L);

/// Build an ElementCount from loop vectorize width metadata, if present.
///
/// Find a combination of metadata ("llvm.loop.vectorize.width" and
/// "llvm.loop.vectorize.scalable.enable") for a loop and use it to construct a
/// ElementCount. If scalable.enable is present the count is scalable; if
/// scalable.disable is present or the tag is absent, it is fixed-width. If the
/// metadata "llvm.loop.vectorize.width" cannot be found then std::nullopt is
/// returned.
///
/// @param TheLoop Loop whose vectorize metadata is inspected.
/// @return The ElementCount from vectorize metadata, or std::nullopt if absent.
LLVM_ABI std::optional<ElementCount>
getOptionalElementCountLoopAttribute(const Loop *TheLoop);

/// Create a new loop identifier for a loop created from a loop transformation.
///
/// @param OrigLoopID The loop ID of the loop before the transformation.
/// @param FollowupAttrs List of attribute names that contain attributes to be
///                      added to the new loop ID.
/// @param InheritOptionsAttrsPrefix Selects which attributes should be inherited
///                                  from the original loop. The following values
///                                  are considered:
///        nullptr   : Inherit all attributes from @p OrigLoopID.
///        ""        : Do not inherit any attribute from @p OrigLoopID; only use
///                    those specified by a followup attribute.
///        "<prefix>": Inherit all attributes except those which start with
///                    <prefix>; commonly used to remove metadata for the
///                    applied transformation.
/// @param AlwaysNew If true, do not try to reuse OrigLoopID and never return
///                  std::nullopt.
///
/// @return The loop ID for the after-transformation loop. The following values
///         can be returned:
///         std::nullopt : No followup attribute was found; it is up to the
///                        transformation to choose attributes that make sense.
///         @p OrigLoopID: The original identifier can be reused.
///         nullptr      : The new loop has no attributes.
///         MDNode*      : A new unique loop identifier.
LLVM_ABI std::optional<MDNode *>
makeFollowupLoopID(MDNode *OrigLoopID, ArrayRef<StringRef> FollowupAttrs,
                   const char *InheritOptionsAttrsPrefix = "",
                   bool AlwaysNew = false);

/// Look for the loop attribute that disables all transformation heuristic.
///
/// @param L The loop whose metadata is inspected.
/// @return True if all transform heuristics are disabled for \p L.
LLVM_ABI bool hasDisableAllTransformsHint(const Loop *L);

/// Look for the loop attribute that disables the LICM transformation heuristics.
///
/// @param L The loop whose metadata is inspected.
/// @return True if LICM transform heuristics are disabled for \p L.
LLVM_ABI bool hasDisableLICMTransformsHint(const Loop *L);

/// The mode sets how eager a transformation should be applied.
enum TransformationMode {
  /// The pass can use heuristics to determine whether a transformation should
  /// be applied.
  TM_Unspecified,

  /// The transformation should be applied without considering a cost model.
  TM_Enable,

  /// The transformation should not be applied.
  TM_Disable,

  /// Force is a flag and should not be used alone.
  TM_Force = 0x04,

  /// The transformation was directed by the user, e.g. by a #pragma in
  /// the source code. If the transformation could not be applied, a
  /// warning should be emitted.
  TM_ForcedByUser = TM_Enable | TM_Force,

  /// The transformation must not be applied. For instance, `#pragma clang loop
  /// unroll(disable)` explicitly forbids any unrolling to take place. Unlike
  /// general loop metadata, it must not be dropped. Most passes should not
  /// behave differently under TM_Disable and TM_SuppressedByUser.
  TM_SuppressedByUser = TM_Disable | TM_Force
};

/// Return a short prefix describing the loop's vectorizer origin.
///
/// Based on the \c llvm.loop.vectorize.body and
/// \c llvm.loop.vectorize.epilogue metadata. The result is one of
/// \c "vectorized epilogue ", \c "vectorized ", \c "epilogue ", or \c ""
/// (empty) and is intended to be prepended to loop-kind tokens in optimization
/// remarks.
///
/// @param L The loop whose vectorize metadata is inspected.
/// @return A short prefix describing the loop's vectorizer origin.
LLVM_ABI StringRef getLoopVectorizeKindPrefix(const Loop *L);

/// Get the unroll transformation mode for \p L.
///
/// @param L The loop to query.
/// @return The unroll transformation mode for \p L.
LLVM_ABI TransformationMode hasUnrollTransformation(const Loop *L);
/// Get the unroll-and-jam transformation mode for \p L.
///
/// @param L The loop to query.
/// @return The unroll-and-jam transformation mode for \p L.
LLVM_ABI TransformationMode hasUnrollAndJamTransformation(const Loop *L);
/// Get the vectorize transformation mode for \p L.
///
/// @param L The loop to query.
/// @return The vectorize transformation mode for \p L.
LLVM_ABI TransformationMode hasVectorizeTransformation(const Loop *L);
/// Get the distribute transformation mode for \p L.
///
/// @param L The loop to query.
/// @return The distribute transformation mode for \p L.
LLVM_ABI TransformationMode hasDistributeTransformation(const Loop *L);
/// Get the LICM versioning transformation mode for \p L.
///
/// @param L The loop to query.
/// @return The LICM versioning transformation mode for \p L.
LLVM_ABI TransformationMode hasLICMVersioningTransformation(const Loop *L);

/// Set input string into loop metadata by keeping other values intact.
/// If the string is already in loop metadata update value if it is
/// different.
///
/// @param TheLoop Loop whose metadata is updated.
/// @param MDString Metadata name to set or update.
/// @param V Integer operand stored with \p MDString.
LLVM_ABI void addStringMetadataToLoop(Loop *TheLoop, const char *MDString,
                                      unsigned V = 0);

/// Add a name-only metadata string to a loop, if not already present.
///
/// Add a single-operand (name-only) node \p MDString to the loop metadata of
/// \p TheLoop, keeping other values intact. If a name-only node with the same
/// string is already present, this is a no-op.
///
/// @param TheLoop Loop whose metadata is updated.
/// @param MDString Name-only metadata string to add.
LLVM_ABI void addStringMetadataToLoop(Loop *TheLoop, StringRef MDString);

/// Return an estimated trip count for \p L, or std::nullopt if unavailable.
///
/// Return either:
/// - \c std::nullopt, if the implementation is unable to handle the loop form
///   of \p L (e.g., \p L must have a latch block that controls the loop exit).
/// - The value of \c llvm.loop.estimated_trip_count from the loop metadata of
///   \p L, if that metadata is present.
/// - Else, a new estimate of the trip count from the latch branch weights of
///   \p L.
///
/// An estimate of zero is meaningful: it indicates that \p L is estimated not
/// to be entered, that is, that its header is not reached.  For example, after
/// peeling 10 or more iterations from a loop with an estimated trip count of
/// 10, \c llvm.loop.estimated_trip_count becomes 0 on the remaining loop.
/// Callers that need a positive trip count must check for zero.
///
/// An estimated trip count is saturated at \c UINT_MAX.
///
/// In addition, if \p EstimatedLoopInvocationWeight, then either:
/// - Set \c *EstimatedLoopInvocationWeight to the weight of the latch's branch
///   to the loop exit.
/// - Do not set it, and return \c std::nullopt, if the current implementation
///   cannot compute that weight (e.g., if \p L does not have a latch block that
///   controls the loop exit) or the weight is zero (because zero cannot be
///   used to compute new branch weights that reflect the estimated trip count).
///
/// TODO: Eventually, once all passes have migrated away from setting branch
/// weights to indicate estimated trip counts, this function will drop the
/// \p EstimatedLoopInvocationWeight parameter.
///
/// @param L The loop whose trip count is estimated.
/// @param EstimatedLoopInvocationWeight Optional out-parameter for latch exit
///                                      weight.
/// @return The estimated trip count, or std::nullopt if unavailable.
LLVM_ABI std::optional<unsigned>
getLoopEstimatedTripCount(Loop *L,
                          unsigned *EstimatedLoopInvocationWeight = nullptr);

/// Set the estimated trip count metadata (and optional latch weights) for \p L.
///
/// Set \c llvm.loop.estimated_trip_count with the value \p EstimatedTripCount
/// in the loop metadata of \p L.  Return false if the implementation is unable
/// to handle the loop form of \p L (e.g., \p L must have a latch block that
/// controls the loop exit).  Otherwise, return true.
///
/// In addition, if \p EstimatedLoopInvocationWeight:
/// - Set the branch weight metadata of \p L to reflect that \p L has an
///   estimated \p EstimatedTripCount iterations and has
///   \c *EstimatedLoopInvocationWeight exit weight through the loop's latch.
/// - If \p EstimatedTripCount is zero, set the backedge weight to 0 and exit
///   edge to 1. The \p EstimatedTripCount is relative to the original loop
///   entry, but the branch weights are encoding the probabilities of the
///   true/false edges. The latter cannot validly be 0-0, because *if* the
///   control flow arrived here, one of the branches *must* be taken. Moreover,
///   BranchProbabilityInfo treats 0-0 branch weights as if they were 1-1.
///   Assuming accurate profile information, a 0 \p EstimatedTripCount should
///   correspond to a very low, or 0, BFI for the loop body. This should mean
///   that the BPI info leading to the loop also gives a very low, or 0,
///   probability to arriving there. If that probability is not exactly 0, 0-0
///   branch weights would raise the BFI of the loop (as it would really be
///   treated as 1-1). With the 0-1 (i.e. 100% exit) encoding, the BFI stays as
///   low as the rest of the CFG's BPI dictates.
///
/// TODO: Eventually, once all passes have migrated away from setting branch
/// weights to indicate estimated trip counts, this function will drop the
/// \p EstimatedLoopInvocationWeight parameter.
///
/// @param L The loop whose estimated trip count is set.
/// @param EstimatedTripCount Estimated iterations to store on the loop.
/// @param EstimatedLoopInvocationWeight Optional latch exit weight used to
///                                      rebuild branch weights.
/// @return True if the estimated trip count was set; false if unsupported.
LLVM_ABI bool setLoopEstimatedTripCount(
    Loop *L, unsigned EstimatedTripCount,
    std::optional<unsigned> EstimatedLoopInvocationWeight = std::nullopt);

/// Return the probability that the latch of \p L continues the loop.
///
/// Based on branch weight metadata, return either:
/// - An unknown probability if the implementation is unable to handle the loop
///   form of \p L (e.g., \p L must have a latch block that controls the loop
///   exit).
/// - The probability \c P that, at the end of any iteration, the latch of \p L
///   will start another iteration such that `1 - P` is the probability of
///   exiting the loop.
///
/// @param L The loop whose latch continuation probability is queried.
/// @return The latch continuation probability, or unknown if unsupported.
LLVM_ABI BranchProbability getLoopProbability(Loop *L);

/// Set latch branch weights of \p L from continuation probability \p P.
///
/// Set branch weight metadata for the latch of \p L to indicate that, at the
/// end of any iteration, \p P and `1 - P` are the probabilities of starting
/// another iteration and exiting the loop, respectively.  Return false if the
/// implementation is unable to handle the loop form of \p L (e.g., \p L must
/// have a latch block that controls the loop exit).  Otherwise, return true.
///
/// @param L The loop whose latch weights are updated.
/// @param P Probability of taking the backedge / continuing the loop.
/// @return True if the latch weights were set; false if unsupported.
LLVM_ABI bool setLoopProbability(Loop *L, BranchProbability P);

/// Return the probability that conditional branch \p B takes one successor.
///
/// Based on branch weight metadata, return either:
/// - An unknown probability if the implementation cannot extract the
///   probability (e.g., \p B must have exactly two target labels, so it must be
///   a conditional branch).
/// - The probability \c P that control flows from \p B to its first target
///   label such that `1 - P` is the probability of control flowing to its
///   second target label, or vice-versa if \p ForFirstTarget is false.
///
/// @param B Conditional branch with exactly two successors.
/// @param ForFirstTarget If true, return the probability of the first
///                       successor; otherwise the second.
/// @return The selected successor probability, or unknown if unsupported.
LLVM_ABI BranchProbability getBranchProbability(CondBrInst *B,
                                                bool ForFirstTarget);

/// Return the edge probability from \p Src to successor \p Dst.
///
/// Calculates the edge probability from Src to Dst. Dst has to be a successor
/// to Src. This uses branch_weights metadata directly. If data are missing or
/// probability cannot be computed, then unknown probability is returned. This
/// does not use BranchProbabilityInfo and the values computed by this will vary
/// from BPI because BPI has its own more advanced heuristics to determine
/// probabilities even without branch_weights metadata.
///
/// @param Src Source basic block of the CFG edge.
/// @param Dst Successor basic block of the CFG edge.
/// @return The edge probability from \p Src to \p Dst, or unknown if
///         unavailable.
LLVM_ABI BranchProbability getBranchProbability(BasicBlock *Src,
                                                BasicBlock *Dst);

/// Set branch weights on \p B from probability \p P for one successor.
///
/// Set branch weight metadata for \p B to indicate that \p P and `1 - P` are
/// the probabilities of control flowing to its first and second target labels,
/// respectively, or vice-versa if \p ForFirstTarget is false.
///
/// @param B Conditional branch with exactly two successors.
/// @param P Probability assigned to the selected successor.
/// @param ForFirstTarget If true, \p P applies to the first successor;
///                       otherwise to the second.
LLVM_ABI void setBranchProbability(CondBrInst *B, BranchProbability P,
                                   bool ForFirstTarget);

/// Check inner loop (L) backedge count is known to be invariant on all
/// iterations of its outer loop. If the loop has no parent, this is trivially
/// true.
///
/// @param L The inner loop whose backedge count is checked.
/// @param SE ScalarEvolution used to prove invariance.
/// @return True if \p L's backedge count is invariant in its parent loop.
LLVM_ABI bool hasIterationCountInvariantInParent(Loop *L, ScalarEvolution &SE);

/// Helper to consistently add the set of standard passes to a loop pass's \c
/// AnalysisUsage.
///
/// All loop passes should call this as part of implementing their \c
/// getAnalysisUsage.
///
/// @param AU AnalysisUsage to populate with standard loop-pass requirements.
LLVM_ABI void getLoopAnalysisUsage(AnalysisUsage &AU);

/// Return true if \p I may legally be sunk or hoisted, ignoring faults.
///
/// Returns true if is legal to hoist or sink this instruction disregarding the
/// possible introduction of faults.  Reasoning about potential faulting
/// instructions is the responsibility of the caller since it is challenging to
/// do efficiently from within this routine.
/// \p TargetExecutesOncePerLoop is true only when it is guaranteed that the
/// target executes at most once per execution of the loop body.  This is used
/// to assess the legality of duplicating atomic loads.  Generally, this is
/// true when moving out of loop and not true when moving into loops.
/// If \p ORE is set use it to emit optimization remarks.
///
/// @param I Instruction considered for sinking or hoisting.
/// @param AA Alias analysis results.
/// @param DT Dominator tree for the function.
/// @param CurLoop Loop containing \p I.
/// @param MSSAU MemorySSA updater used for memory dependence checks.
/// @param TargetExecutesOncePerLoop Whether the move target runs once per loop.
/// @param LICMFlags Caps controlling MemorySSA query cost.
/// @param ORE Optional remark emitter for diagnostics.
/// @return True if \p I may legally be sunk or hoisted, ignoring faults.
LLVM_ABI bool canSinkOrHoistInst(Instruction &I, AAResults *AA,
                                 DominatorTree *DT, Loop *CurLoop,
                                 MemorySSAUpdater &MSSAU,
                                 bool TargetExecutesOncePerLoop,
                                 SinkAndHoistLICMFlags &LICMFlags,
                                 OptimizationRemarkEmitter *ORE = nullptr);

/// Return true if load \p LI may legally be hoisted out of \p CurLoop.
///
/// This is the load-specific subset of \c canSinkOrHoistInst: it rejects
/// volatile or ordered loads, allows constant-memory / invariant.load /
/// invariant.start-dominated loads unconditionally, and otherwise queries
/// \p MSSA for an in-loop clobber. \p TargetExecutesOncePerLoop has the same
/// meaning as in \c canSinkOrHoistInst (set to true when hoisting to the
/// preheader).
///
/// @param LI Load considered for hoisting.
/// @param AA Alias analysis results.
/// @param DT Dominator tree for the function.
/// @param CurLoop Loop containing \p LI.
/// @param MSSA MemorySSA used to find in-loop clobbers.
/// @param TargetExecutesOncePerLoop Whether the hoist target runs once per
///                                  loop.
/// @param LICMFlags Caps controlling MemorySSA query cost.
/// @param ORE Optional remark emitter for diagnostics.
/// @return True if \p LI may legally be hoisted out of \p CurLoop.
LLVM_ABI bool canHoistLoad(LoadInst &LI, AAResults *AA, DominatorTree *DT,
                           Loop *CurLoop, MemorySSA &MSSA,
                           bool TargetExecutesOncePerLoop,
                           SinkAndHoistLICMFlags &LICMFlags,
                           OptimizationRemarkEmitter *ORE = nullptr);

/// Returns the llvm.vector.reduce intrinsic that corresponds to the recurrence
/// kind.
///
/// @param RK Recurrence kind to map to a reduction intrinsic.
/// @return The llvm.vector.reduce intrinsic id for \p RK.
LLVM_ABI constexpr Intrinsic::ID getReductionIntrinsicID(RecurKind RK);
/// Returns the llvm.vector.reduce min/max intrinsic that corresponds to the
/// intrinsic op.
///
/// @param IID Min/max intrinsic opcode to map to a reduction intrinsic.
/// @return The llvm.vector.reduce min/max intrinsic id for \p IID.
LLVM_ABI Intrinsic::ID getMinMaxReductionIntrinsicID(Intrinsic::ID IID);

/// Returns the arithmetic instruction opcode used when expanding a reduction.
///
/// @param RdxID Reduction intrinsic id being expanded.
/// @return The arithmetic opcode used when expanding the reduction.
LLVM_ABI unsigned getArithmeticReductionInstruction(Intrinsic::ID RdxID);
/// Returns the reduction intrinsic id corresponding to the binary operation.
///
/// @param Opc Binary opcode to map to a reduction intrinsic.
/// @return The reduction intrinsic id corresponding to \p Opc.
LLVM_ABI Intrinsic::ID getReductionForBinop(Instruction::BinaryOps Opc);

/// Returns the min/max intrinsic used when expanding a min/max reduction.
///
/// @param RdxID Reduction intrinsic id being expanded.
/// @return The min/max intrinsic used when expanding the reduction.
LLVM_ABI Intrinsic::ID getMinMaxReductionIntrinsicOp(Intrinsic::ID RdxID);

/// Returns the min/max intrinsic used when expanding a min/max reduction.
///
/// @param RK Recurrence kind describing the min/max reduction.
/// @return The min/max intrinsic used when expanding the reduction.
LLVM_ABI Intrinsic::ID getMinMaxReductionIntrinsicOp(RecurKind RK);

/// Returns the recurence kind used when expanding a min/max reduction.
///
/// @param RdxID Reduction intrinsic id being expanded.
/// @return The recurrence kind for the min/max reduction.
LLVM_ABI RecurKind getMinMaxReductionRecurKind(Intrinsic::ID RdxID);

/// Returns the comparison predicate used when expanding a min/max reduction.
///
/// @param RK Recurrence kind describing the min/max reduction.
/// @return The comparison predicate for the min/max reduction.
LLVM_ABI CmpInst::Predicate getMinMaxReductionPredicate(RecurKind RK);

/// Given information about an @llvm.vector.reduce.* intrinsic, return
/// the identity value for the reduction.
///
/// @param RdxID Reduction intrinsic id whose identity is requested.
/// @param Ty Result type of the identity value.
/// @param FMF Fast-math flags affecting floating-point identities.
/// @return The identity value for the reduction intrinsic.
LLVM_ABI Value *getReductionIdentity(Intrinsic::ID RdxID, Type *Ty,
                                     FastMathFlags FMF);

/// Given information about an recurrence kind, return the identity
/// for the @llvm.vector.reduce.* used to generate it.
///
/// @param K Recurrence kind whose identity is requested.
/// @param Tp Result type of the identity value.
/// @param FMF Fast-math flags affecting floating-point identities.
/// @return The identity value for the recurrence kind.
LLVM_ABI Value *getRecurrenceIdentity(RecurKind K, Type *Tp, FastMathFlags FMF);

/// Returns a Min/Max operation corresponding to MinMaxRecurrenceKind.
/// The Builder's fast-math-flags must be set to propagate the expected values.
///
/// @param Builder IR builder used to create the min/max operation.
/// @param RK Recurrence kind selecting the min/max variant.
/// @param Left Left-hand operand.
/// @param Right Right-hand operand.
/// @return The created min/max operation.
LLVM_ABI Value *createMinMaxOp(IRBuilderBase &Builder, RecurKind RK,
                               Value *Left, Value *Right);

/// Generates an ordered vector reduction using extracts to reduce the value.
///
/// @param Builder IR builder used to create the reduction.
/// @param Acc Initial accumulator value.
/// @param Src Vector value being reduced.
/// @param Op Arithmetic opcode applied element-wise.
/// @param MinMaxKind Optional min/max recurrence kind when \p Op is a compare.
/// @return The ordered reduction result.
LLVM_ABI Value *getOrderedReduction(IRBuilderBase &Builder, Value *Acc,
                                    Value *Src, unsigned Op,
                                    RecurKind MinMaxKind = RecurKind::None);

/// Expand a scalable vector reduction into an element-wise runtime loop.
///
/// Expand a scalable vector reduction into a runtime loop that applies
/// \p RdxOpcode element by element, starting from \p Acc as the initial
/// accumulator value (typically the reduction identity). If \p DT and/or \p LI
/// are provided, they are updated to reflect the new basic blocks.
///
/// @param Builder IR builder positioned where the loop should be emitted.
/// @param Vec Scalable vector value being reduced.
/// @param RdxOpcode Opcode applied to each extracted element.
/// @param Acc Initial accumulator / identity value.
/// @param DT Optional dominator tree to update.
/// @param LI Optional loop info to update.
/// @return The reduced scalar value.
LLVM_ABI Value *expandReductionViaLoop(IRBuilderBase &Builder, Value *Vec,
                                       unsigned RdxOpcode, Value *Acc,
                                       DominatorTree *DT = nullptr,
                                       LoopInfo *LI = nullptr);

/// Generates a vector reduction using shufflevectors to reduce the value.
/// Fast-math-flags are propagated using the IRBuilder's setting.
///
/// @param Builder IR builder used to create the shuffle reduction.
/// @param Src Vector value being reduced.
/// @param Op Arithmetic opcode applied during the reduction.
/// @param RS Shuffle reduction strategy from TTI.
/// @param MinMaxKind Optional min/max recurrence kind when \p Op is a compare.
/// @return The shuffle reduction result.
LLVM_ABI Value *getShuffleReduction(IRBuilderBase &Builder, Value *Src,
                                    unsigned Op,
                                    TargetTransformInfo::ReductionShuffle RS,
                                    RecurKind MinMaxKind = RecurKind::None);

/// Create a vector reduction for recurrence kind \p RdxKind.
///
/// Create a reduction of the given vector. The reduction operation is described
/// by the \p Opcode parameter. min/max reductions require additional
/// information supplied in \p RdxKind. Fast-math-flags are propagated using the
/// IRBuilder's setting.
///
/// @param B IR builder used to create the reduction.
/// @param Src Vector value being reduced.
/// @param RdxKind Recurrence kind describing the reduction.
/// @return The reduction result.
LLVM_ABI Value *createSimpleReduction(IRBuilderBase &B, Value *Src,
                                      RecurKind RdxKind);
/// Overloaded function to generate vector-predication intrinsics for
/// reduction.
///
/// @param B IR builder used to create the reduction.
/// @param Src Vector value being reduced.
/// @param RdxKind Recurrence kind describing the reduction.
/// @param Mask Predication mask for active lanes.
/// @param EVL Explicit vector length for the reduction.
/// @return The predicated reduction result.
LLVM_ABI Value *createSimpleReduction(IRBuilderBase &B, Value *Src,
                                      RecurKind RdxKind, Value *Mask,
                                      Value *EVL);

/// Create a reduction of the given vector \p Src for a reduction of kind
/// RecurKind::AnyOf. The start value of the reduction is \p InitVal.
///
/// @param B IR builder used to create the reduction.
/// @param Src Vector value being reduced.
/// @param InitVal Initial / start value of the AnyOf reduction.
/// @param OrigPhi Original reduction PHI used to recover context.
/// @return The AnyOf reduction result.
LLVM_ABI Value *createAnyOfReduction(IRBuilderBase &B, Value *Src,
                                     Value *InitVal, PHINode *OrigPhi);

/// Create an ordered reduction intrinsic using the given recurrence
/// kind \p RdxKind.
///
/// @param B IR builder used to create the reduction.
/// @param RdxKind Recurrence kind describing the ordered reduction.
/// @param Src Vector value being reduced.
/// @param Start Initial accumulator value.
/// @return The ordered reduction result.
LLVM_ABI Value *createOrderedReduction(IRBuilderBase &B, RecurKind RdxKind,
                                       Value *Src, Value *Start);
/// Overloaded function to generate vector-predication intrinsics for ordered
/// reduction.
///
/// @param B IR builder used to create the reduction.
/// @param RdxKind Recurrence kind describing the ordered reduction.
/// @param Src Vector value being reduced.
/// @param Start Initial accumulator value.
/// @param Mask Predication mask for active lanes.
/// @param EVL Explicit vector length for the reduction.
/// @return The predicated ordered reduction result.
LLVM_ABI Value *createOrderedReduction(IRBuilderBase &B, RecurKind RdxKind,
                                       Value *Src, Value *Start, Value *Mask,
                                       Value *EVL);

/// Intersect IR flags from scalar ops \p VL onto vector op \p I.
///
/// Get the intersection (logical and) of all of the potential IR flags of each
/// scalar operation (VL) that will be converted into a vector (I). If OpValue
/// is non-null, we only consider operations similar to OpValue when
/// intersecting. Flag set: NSW, NUW (if IncludeWrapFlags is true), exact, and
/// all of fast-math.
///
/// @param I Vector instruction that receives the intersected flags.
/// @param VL Scalar operations whose flags are intersected.
/// @param OpValue Optional prototype value; only similar ops are considered.
/// @param IncludeWrapFlags Whether NSW/NUW wrap flags are included.
LLVM_ABI void propagateIRFlags(Value *I, ArrayRef<Value *> VL,
                               Value *OpValue = nullptr,
                               bool IncludeWrapFlags = true);

/// Returns true if we can prove that \p S is defined and always negative in
/// loop \p L.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @return True if \p S is defined and always negative in \p L.
LLVM_ABI bool isKnownNegativeInLoop(const SCEV *S, const Loop *L,
                                    ScalarEvolution &SE);

/// Returns true if we can prove that \p S is defined and always non-negative in
/// loop \p L.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @return True if \p S is defined and always non-negative in \p L.
LLVM_ABI bool isKnownNonNegativeInLoop(const SCEV *S, const Loop *L,
                                       ScalarEvolution &SE);
/// Returns true if we can prove that \p S is defined and always positive in
/// loop \p L.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @return True if \p S is defined and always positive in \p L.
LLVM_ABI bool isKnownPositiveInLoop(const SCEV *S, const Loop *L,
                                    ScalarEvolution &SE);

/// Returns true if we can prove that \p S is defined and always non-positive in
/// loop \p L.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @return True if \p S is defined and always non-positive in \p L.
LLVM_ABI bool isKnownNonPositiveInLoop(const SCEV *S, const Loop *L,
                                       ScalarEvolution &SE);

/// Returns true if \p S is defined and never is equal to signed/unsigned max.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @param Signed If true, compare against signed max; else unsigned max.
/// @return True if \p S is defined and never equals signed/unsigned max.
LLVM_ABI bool cannotBeMaxInLoop(const SCEV *S, const Loop *L,
                                ScalarEvolution &SE, bool Signed);

/// Returns true if \p S is defined and never is equal to signed/unsigned min.
///
/// @param S SCEV expression to classify.
/// @param L Loop in which \p S is evaluated.
/// @param SE ScalarEvolution used for the proof.
/// @param Signed If true, compare against signed min; else unsigned min.
/// @return True if \p S is defined and never equals signed/unsigned min.
LLVM_ABI bool cannotBeMinInLoop(const SCEV *S, const Loop *L,
                                ScalarEvolution &SE, bool Signed);

/// Strategy for replacing loop exit values with SCEV expansions.
enum ReplaceExitVal {
  /// Never replace exit values.
  NeverRepl,
  /// Replace exit values only when expansion is cheap.
  OnlyCheapRepl,
  /// Replace exit values when the loop definition is likely dead.
  NoHardUse,
  /// Replace unused induction-variable exit values with cheap expansions.
  UnusedIndVarInLoop,
  /// Always replace exit values when possible.
  AlwaysRepl
};

/// Rewrite computable loop exit values into users outside the loop.
///
/// If the final value of any expressions that are recurrent in the loop can be
/// computed, substitute the exit values from the loop into any instructions
/// outside of the loop that use the final values of the current expressions.
/// Return the number of loop exit values that have been replaced, and the
/// corresponding phi node will be added to DeadInsts.
///
/// @param L Loop whose exit values may be rewritten.
/// @param LI Loop info for the function.
/// @param TLI Target library info.
/// @param SE ScalarEvolution used to compute exit values.
/// @param TTI Target transform info for expansion cost.
/// @param Rewriter SCEV expander used to materialize exit values.
/// @param DT Dominator tree for the function.
/// @param ReplaceExitValue Policy controlling which exits are rewritten.
/// @param DeadInsts Collects PHI nodes made dead by the rewrite.
/// @return The number of loop exit values that were replaced.
LLVM_ABI int rewriteLoopExitValues(Loop *L, LoopInfo *LI,
                                   TargetLibraryInfo *TLI, ScalarEvolution *SE,
                                   const TargetTransformInfo *TTI,
                                   SCEVExpander &Rewriter, DominatorTree *DT,
                                   ReplaceExitVal ReplaceExitValue,
                                   SmallVector<WeakTrackingVH, 16> &DeadInsts);

/// Append loops from a range onto a worklist in reverse postorder.
///
/// Utility that implements appending of loops onto a worklist given a range.
/// We want to process loops in postorder, but the worklist is a LIFO data
/// structure, so we append to it in *reverse* postorder. For trees, a preorder
/// traversal is a viable reverse postorder, so we actually append using a
/// preorder walk algorithm.
///
/// @param Loops Range of loops to append.
/// @param Worklist LIFO worklist that receives the loops.
template <typename RangeT>
LLVM_TEMPLATE_ABI void
appendLoopsToWorklist(RangeT &&Loops, SmallPriorityWorklist<Loop *, 4> &Worklist);
/// Append an already-reversed range of loops onto a worklist.
///
/// Utility that implements appending of loops onto a worklist given a range.
/// It has the same behavior as appendLoopsToWorklist, but assumes the range of
/// loops has already been reversed, so it processes loops in the given order.
///
/// @param Loops Already-reversed range of loops to append.
/// @param Worklist LIFO worklist that receives the loops.
template <typename RangeT>
void appendReversedLoopsToWorklist(RangeT &&Loops,
                                   SmallPriorityWorklist<Loop *, 4> &Worklist);

extern template LLVM_TEMPLATE_ABI void
appendLoopsToWorklist<ArrayRef<Loop *> &>(
    ArrayRef<Loop *> &Loops, SmallPriorityWorklist<Loop *, 4> &Worklist);

extern template LLVM_TEMPLATE_ABI void
appendLoopsToWorklist<Loop &>(Loop &L,
                              SmallPriorityWorklist<Loop *, 4> &Worklist);

/// Append all loops from \p LI onto a worklist in CFG-forward order.
///
/// Utility that implements appending of loops onto a worklist given LoopInfo.
/// Calls the templated utility taking a Range of loops, handing it the Loops
/// in LoopInfo, iterated in reverse. This is because the loops are stored in
/// RPO w.r.t. the control flow graph in LoopInfo. For the purpose of unrolling,
/// loop deletion, and LICM, we largely want to work forward across the CFG so
/// that we visit defs before uses and can propagate simplifications from one
/// loop nest into the next. Calls appendReversedLoopsToWorklist with the
/// already reversed loops in LI.
/// FIXME: Consider changing the order in LoopInfo.
///
/// @param LI Loop info whose loops are appended.
/// @param Worklist LIFO worklist that receives the loops.
LLVM_ABI void appendLoopsToWorklist(LoopInfo &LI,
                                    SmallPriorityWorklist<Loop *, 4> &Worklist);

/// Recursively clone the specified loop and all of its children,
/// mapping the blocks with the specified map.
///
/// @param L Loop to clone.
/// @param PL Parent loop that receives the clone, or nullptr for top-level.
/// @param VM Value map populated for the cloned blocks and values.
/// @param LI Loop info updated with the cloned loop.
/// @param LPM Optional loop pass manager to notify, or nullptr.
/// @return The cloned loop.
LLVM_ABI Loop *cloneLoop(Loop *L, Loop *PL, ValueToValueMapTy &VM, LoopInfo *LI,
                         LPPassManager *LPM);

/// Add code that checks at runtime if the accessed arrays in \p PointerChecks
/// overlap. Returns the final comparator value or NULL if no check is needed.
///
/// @param Loc Insertion point for the runtime checks.
/// @param TheLoop Loop whose pointer groups are being checked.
/// @param PointerChecks Pairs of pointer groups that may need checks.
/// @param Expander SCEV expander used to materialize bounds.
/// @param HoistRuntimeChecks Whether checks may be hoisted when profitable.
/// @return The final comparator value, or nullptr if no check is needed.
LLVM_ABI Value *
addRuntimeChecks(Instruction *Loc, Loop *TheLoop,
                 const SmallVectorImpl<RuntimePointerCheck> &PointerChecks,
                 SCEVExpander &Expander, bool HoistRuntimeChecks = false);

/// Add runtime checks that compare pointer differences against a distance.
///
/// @param Loc Insertion point for the runtime checks.
/// @param Checks Pointer-difference descriptors to validate.
/// @param Expander SCEV expander used to materialize addresses.
/// @param VF Vectorization factor used when scaling the distance.
/// @param IC Interleave count used when scaling the distance.
/// @return The final comparator value, or nullptr if no check is needed.
LLVM_ABI Value *addDiffRuntimeChecks(Instruction *Loc,
                                     ArrayRef<PointerDiffInfo> Checks,
                                     SCEVExpander &Expander, ElementCount VF,
                                     unsigned IC);

/// Struct to hold information about a partially invariant condition.
struct IVConditionInfo {
  /// Instructions that need to be duplicated and checked for the unswitching
  /// condition.
  SmallVector<Instruction *> InstToDuplicate;

  /// Constant to indicate for which value the condition is invariant.
  Constant *KnownValue = nullptr;

  /// True if the partially invariant path is no-op (=does not have any
  /// side-effects and no loop value is used outside the loop).
  bool PathIsNoop = true;

  /// If the partially invariant path reaches a single exit block, ExitForPath
  /// is set to that block. Otherwise it is nullptr.
  BasicBlock *ExitForPath = nullptr;
};

/// Detect a partially invariant header condition suitable for unswitching.
///
/// Check if the loop header has a conditional branch that is not
/// loop-invariant, because it involves load instructions. If all paths from
/// either the true or false successor to the header or loop exits do not
/// modify the memory feeding the condition, perform 'partial unswitching'. That
/// is, duplicate the instructions feeding the condition in the pre-header. Then
/// unswitch on the duplicated condition. The condition is now known in the
/// unswitched version for the 'invariant' path through the original loop.
///
/// If the branch condition of the header is partially invariant, return a pair
/// containing the instructions to duplicate and a boolean Constant to update
/// the condition in the loops created for the true or false successors.
///
/// @param L Loop whose header condition is analyzed.
/// @param MSSAThreshold Cap on MemorySSA clobbering queries.
/// @param MSSA MemorySSA used to prove memory invariance of the condition.
/// @param AA Alias analysis used together with MemorySSA.
/// @return Partial-invariance info when applicable; otherwise std::nullopt.
LLVM_ABI std::optional<IVConditionInfo>
hasPartialIVCondition(const Loop &L, unsigned MSSAThreshold,
                      const MemorySSA &MSSA, AAResults &AA);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOOPUTILS_H
