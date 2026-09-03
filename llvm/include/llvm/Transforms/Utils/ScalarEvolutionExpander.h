//===---- llvm/Analysis/ScalarEvolutionExpander.h - SCEV Exprs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to generate code from scalar expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SCALAREVOLUTIONEXPANDER_H
#define LLVM_TRANSFORMS_UTILS_SCALAREVOLUTIONEXPANDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {
/// Command-line option for the budget used to decide if a SCEV expansion is
/// cheap.
LLVM_ABI extern cl::opt<unsigned> SCEVCheapExpansionBudget;

/// struct for holding enough information to help calculate the cost of the
/// given SCEV when expanded into IR.
struct SCEVOperand {
  /// Construct a SCEVOperand describing one operand of an expanded instruction.
  ///
  /// \param Opc Parent LLVM instruction opcode.
  /// \param Idx Operand index within the parent instruction.
  /// \param S SCEV for this operand.
  explicit SCEVOperand(unsigned Opc, int Idx, const SCEV *S) :
    ParentOpcode(Opc), OperandIdx(Idx), S(S) { }
  /// LLVM instruction opcode that uses the operand.
  unsigned ParentOpcode;
  /// The use index of an expanded instruction.
  int OperandIdx;
  /// The SCEV operand to be costed.
  const SCEV* S;
};

/// Captures poison-generating flags from an instruction so they can be restored.
struct PoisonFlags {
  /// No unsigned wrap flag.
  unsigned NUW : 1;
  /// No signed wrap flag.
  unsigned NSW : 1;
  /// Exact division or shift flag.
  unsigned Exact : 1;
  /// Disjoint or flag.
  unsigned Disjoint : 1;
  /// Non-negative flag.
  unsigned NNeg : 1;
  /// Same-sign flag.
  unsigned SameSign : 1;
  /// GEP no-wrap flags.
  GEPNoWrapFlags GEPNW;

  /// Capture poison-generating flags from \p I.
  ///
  /// \param I Instruction whose flags are recorded.
  LLVM_ABI PoisonFlags(const Instruction *I);
  /// Apply the recorded poison-generating flags to \p I.
  ///
  /// \param I Instruction that receives the recorded flags.
  LLVM_ABI void apply(Instruction *I);
};

/// This class uses information about analyze scalars to rewrite expressions
/// in canonical form.
///
/// Clients should create an instance of this class when rewriting is needed,
/// and destroy it when finished to allow the release of the associated
/// memory.
class SCEVExpander : public SCEVUseVisitor<SCEVExpander, Value *> {
  friend class SCEVExpanderCleaner;

  ScalarEvolution &SE;
  const DataLayout &DL;

  // New instructions receive a name to identify them with the current pass.
  const char *IVName;

  /// Indicates whether LCSSA phis should be created for inserted values.
  bool PreserveLCSSA;

  // InsertedExpressions caches Values for reuse, so must track RAUW.
  DenseMap<std::pair<SCEVUse, Instruction *>, TrackingVH<Value>>
      InsertedExpressions;

  // InsertedOverflowChecks caches Values for reuse, so must track RAUW.
  DenseMap<std::tuple<Value *, Value *, Instruction *>,
           std::pair<TrackingVH<Value>, TrackingVH<Value>>>
      InsertedOverflowChecks;

  // InsertedValues only flags inserted instructions so needs no RAUW.
  DenseSet<AssertingVH<Value>> InsertedValues;
  DenseSet<AssertingVH<Value>> InsertedPostIncValues;

  /// Keep track of the existing IR values re-used during expansion.
  /// FIXME: Ideally re-used instructions would not be added to
  /// InsertedValues/InsertedPostIncValues.
  SmallPtrSet<Value *, 16> ReusedValues;

  /// Original flags of instructions for which they were modified. Used
  /// by SCEVExpanderCleaner to undo changes.
  DenseMap<PoisoningVH<Instruction>, PoisonFlags> OrigFlags;

  // The induction variables generated.
  SmallVector<WeakVH, 2> InsertedIVs;

  /// A memoization of the "relevant" loop for a given SCEV.
  DenseMap<const SCEV *, const Loop *> RelevantLoops;

  /// Addrecs referring to any of the given loops are expanded in post-inc
  /// mode. For example, expanding {1,+,1}<L> in post-inc mode returns the add
  /// instruction that adds one to the phi for {0,+,1}<L>, as opposed to a new
  /// phi starting at 1. This is only supported in non-canonical mode.
  PostIncLoopSet PostIncLoops;

  /// When this is non-null, addrecs expanded in the loop it indicates should
  /// be inserted with increments at IVIncInsertPos.
  const Loop *IVIncInsertLoop;

  /// When expanding addrecs in the IVIncInsertLoop loop, insert the IV
  /// increment at this position.
  Instruction *IVIncInsertPos;

  /// Phis that complete an IV chain. Reuse
  DenseSet<AssertingVH<PHINode>> ChainedPhis;

  /// When true, SCEVExpander tries to expand expressions in "canonical" form.
  /// When false, expressions are expanded in a more literal form.
  ///
  /// In "canonical" form addrecs are expanded as arithmetic based on a
  /// canonical induction variable. Note that CanonicalMode doesn't guarantee
  /// that all expressions are expanded in "canonical" form. For some
  /// expressions literal mode can be preferred.
  bool CanonicalMode;

  /// When invoked from LSR, the expander is in "strength reduction" mode. The
  /// only difference is that phi's are only reused if they are already in
  /// "expanded" form.
  bool LSRMode;

  /// When true, rewrite any divisors of UDiv expressions that may be 0 to
  /// umax(Divisor, 1) to avoid introducing UB. If the divisor may be poison,
  /// freeze it first.
  bool SafeUDivMode = false;

  typedef IRBuilder<InstSimplifyFolder, IRBuilderCallbackInserter> BuilderType;
  BuilderType Builder;

  // RAII object that stores the current insertion point and restores it when
  // the object is destroyed. This includes the debug location.  Duplicated
  // from InsertPointGuard to add SetInsertPoint() which is used to updated
  // InsertPointGuards stack when insert points are moved during SCEV
  // expansion.
  class SCEVInsertPointGuard {
    IRBuilderBase &Builder;
    AssertingVH<BasicBlock> Block;
    BasicBlock::iterator Point;
    DebugLoc DbgLoc;
    SCEVExpander *SE;

    SCEVInsertPointGuard(const SCEVInsertPointGuard &) = delete;
    SCEVInsertPointGuard &operator=(const SCEVInsertPointGuard &) = delete;

  public:
    SCEVInsertPointGuard(IRBuilderBase &B, SCEVExpander *SE)
        : Builder(B), Block(B.GetInsertBlock()), Point(B.GetInsertPoint()),
          DbgLoc(B.getCurrentDebugLocation()), SE(SE) {
      SE->InsertPointGuards.push_back(this);
    }

    ~SCEVInsertPointGuard() {
      // These guards should always created/destroyed in FIFO order since they
      // are used to guard lexically scoped blocks of code in
      // ScalarEvolutionExpander.
      assert(SE->InsertPointGuards.back() == this);
      SE->InsertPointGuards.pop_back();
      Builder.restoreIP(IRBuilderBase::InsertPoint(Block, Point));
      Builder.SetCurrentDebugLocation(DbgLoc);
    }

    BasicBlock::iterator GetInsertPoint() const { return Point; }
    void SetInsertPoint(BasicBlock::iterator I) { Point = I; }
  };

  /// Stack of pointers to saved insert points, used to keep insert points
  /// consistent when instructions are moved.
  SmallVector<SCEVInsertPointGuard *, 8> InsertPointGuards;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  const char *DebugType;
#endif

  friend struct SCEVUseVisitor<SCEVExpander, Value *>;

public:
  /// Construct a SCEVExpander in "canonical" mode.
  ///
  /// \param SE ScalarEvolution analysis used for expansions.
  /// \param Name Name prefix applied to newly inserted instructions.
  /// \param PreserveLCSSA Whether to create LCSSA phis for inserted values.
  explicit SCEVExpander(ScalarEvolution &SE, const char *Name,
                        bool PreserveLCSSA = true)
      : SE(SE), DL(SE.getDataLayout()), IVName(Name),
        PreserveLCSSA(PreserveLCSSA), IVIncInsertLoop(nullptr),
        IVIncInsertPos(nullptr), CanonicalMode(true), LSRMode(false),
        Builder(SE.getContext(), InstSimplifyFolder(DL),
                IRBuilderCallbackInserter(
                    [this](Instruction *I) { rememberInstruction(I); })) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    DebugType = "";
#endif
  }

  /// Destroy the expander and assert that insert-point guards were balanced.
  ~SCEVExpander() {
    // Make sure the insert point guard stack is consistent.
    assert(InsertPointGuards.empty());
  }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  void setDebugType(const char *s) { DebugType = s; }
#endif

  /// Clear cached expansions and inserted-value tracking.
  ///
  /// Erase the contents of the InsertedExpressions map so that users trying
  /// to expand the same expression into multiple BasicBlocks or different
  /// places within the same BasicBlock can do so.
  void clear() {
    InsertedExpressions.clear();
    InsertedOverflowChecks.clear();
    InsertedValues.clear();
    InsertedPostIncValues.clear();
    ReusedValues.clear();
    OrigFlags.clear();
    ChainedPhis.clear();
    InsertedIVs.clear();
  }

  /// Return the ScalarEvolution instance used by this expander.
  ///
  /// \return The ScalarEvolution instance used by this expander.
  ScalarEvolution *getSE() { return &SE; }
  /// Return the induction variables created during expansion.
  ///
  /// \return The induction variables created during expansion.
  const SmallVectorImpl<WeakVH> &getInsertedIVs() const { return InsertedIVs; }

  /// Return a vector containing all instructions inserted during expansion.
  ///
  /// \return A vector containing all instructions inserted during expansion.
  SmallVector<Instruction *, 32> getAllInsertedInstructions() const {
    SmallVector<Instruction *, 32> Result;
    for (const auto &VH : InsertedValues) {
      Value *V = VH;
      if (ReusedValues.contains(V))
        continue;
      if (auto *Inst = dyn_cast<Instruction>(V))
        Result.push_back(Inst);
    }
    for (const auto &VH : InsertedPostIncValues) {
      Value *V = VH;
      if (ReusedValues.contains(V))
        continue;
      if (auto *Inst = dyn_cast<Instruction>(V))
        Result.push_back(Inst);
    }

    return Result;
  }

  /// Return true for expressions that can't be evaluated at runtime
  /// within given \b Budget.
  ///
  /// \p At is a parameter which specifies point in code where user is going to
  /// expand these expressions. Sometimes this knowledge can lead to
  /// a less pessimistic cost estimation.
  ///
  /// \param Exprs SCEV expressions to cost.
  /// \param L Loop context for the expansion cost estimate.
  /// \param Budget Maximum allowed expansion cost in TCC_Basic units.
  /// \param TTI Target transform info used for cost modeling.
  /// \param At Instruction at which the expressions would be expanded.
  /// \return True if any expression cannot be evaluated within \p Budget.
  bool isHighCostExpansion(ArrayRef<const SCEV *> Exprs, Loop *L,
                           unsigned Budget, const TargetTransformInfo *TTI,
                           const Instruction *At) {
    assert(TTI && "This function requires TTI to be provided.");
    assert(At && "This function requires At instruction to be provided.");
    if (!TTI)      // In assert-less builds, avoid crashing
      return true; // by always claiming to be high-cost.
    SmallVector<SCEVOperand, 8> Worklist;
    SmallPtrSet<const SCEV *, 8> Processed;
    InstructionCost Cost = 0;
    unsigned ScaledBudget = Budget * TargetTransformInfo::TCC_Basic;
    for (auto *Expr : Exprs)
      Worklist.emplace_back(-1, -1, Expr);
    while (!Worklist.empty()) {
      const SCEVOperand WorkItem = Worklist.pop_back_val();
      if (isHighCostExpansionHelper(WorkItem, L, *At, Cost, ScaledBudget, *TTI,
                                    Processed, Worklist))
        return true;
    }
    assert(Cost <= ScaledBudget && "Should have returned from inner loop.");
    return false;
  }

  /// Return the induction variable increment's IV operand.
  ///
  /// \param IncV Increment instruction to inspect.
  /// \param InsertPos Position used when considering scaled increments.
  /// \param allowScale Whether a scaled GEP operand may be returned.
  /// \return The IV operand of the increment, or nullptr if none.
  LLVM_ABI Instruction *
  getIVIncOperand(Instruction *IncV, Instruction *InsertPos, bool allowScale);

  /// Hoist \p IncV and its required subexpressions before \p InsertPos.
  ///
  /// If \p RecomputePoisonFlags is set, drops all poison-generating flags from
  /// instructions being hoisted and tries to re-infer them in the new location.
  /// It should be used when we are going to introduce a new use in the new
  /// position that didn't exist before, and may trigger new UB in case of
  /// poison.
  ///
  /// \param IncV Induction-variable increment to hoist.
  /// \param InsertPos Instruction before which \p IncV is hoisted.
  /// \param RecomputePoisonFlags Whether to drop and re-infer poison flags.
  /// \return True if the increment was successfully hoisted.
  LLVM_ABI bool hoistIVInc(Instruction *IncV, Instruction *InsertPos,
                           bool RecomputePoisonFlags = false);

  /// Return true if original and wide IV increments can share poison flags.
  ///
  /// Both increments must directly increment the corresponding IV PHI nodes and
  /// have the same opcode. It is not safe to re-use the flags from the original
  /// increment, if it is more complex and SCEV expansion may have yielded a
  /// more simplified wider increment.
  ///
  /// \param OrigPhi Original induction-variable PHI.
  /// \param WidePhi Widened induction-variable PHI.
  /// \param OrigInc Original IV increment instruction.
  /// \param WideInc Widened IV increment instruction.
  /// \return True if the original and wide IV increments can share poison flags.
  LLVM_ABI static bool canReuseFlagsFromOriginalIVInc(PHINode *OrigPhi,
                                                      PHINode *WidePhi,
                                                      Instruction *OrigInc,
                                                      Instruction *WideInc);

  /// replace congruent phis with their most canonical representative. Return
  /// the number of phis eliminated.
  ///
  /// \param L Loop whose congruent IVs are replaced.
  /// \param DT Dominator tree used to choose canonical representatives.
  /// \param DeadInsts Collects instructions that become dead.
  /// \param TTI Optional target transform info for profitability checks.
  /// \return The number of phis eliminated.
  LLVM_ABI unsigned
  replaceCongruentIVs(Loop *L, const DominatorTree *DT,
                      SmallVectorImpl<WeakTrackingVH> &DeadInsts,
                      const TargetTransformInfo *TTI = nullptr);

  /// Return true if \p S is safe to expand anywhere its operands are defined.
  ///
  /// All materialized values must be safe to speculate anywhere their operands
  /// are defined, and the expander must be capable of expanding the expression.
  ///
  /// \param S SCEV expression to test.
  /// \return True if \p S is safe to expand anywhere its operands are defined.
  LLVM_ABI bool isSafeToExpand(const SCEV *S) const;

  /// Return true if \p S is safe to expand at \p InsertionPoint.
  ///
  /// All materialized values must be defined and safe to speculate at the
  /// specified location and their operands must be defined at this location.
  ///
  /// \param S SCEV expression to test.
  /// \param InsertionPoint Location at which expansion would occur.
  /// \return True if \p S is safe to expand at \p InsertionPoint.
  LLVM_ABI bool isSafeToExpandAt(const SCEV *S,
                                 const Instruction *InsertionPoint) const;

  /// Drop poison-generating flags from \p I, then try re-infer via SCEV.
  ///
  /// \param SE ScalarEvolution used to re-infer flags.
  /// \param I Instruction whose poison annotations are refreshed.
  LLVM_ABI static void
  dropPoisonGeneratingAnnotationsAndReinfer(ScalarEvolution &SE,
                                            Instruction *I);

  /// Find an existing cast among \p PtrOp's users that computes the same value
  /// as a `ptrtoaddr` of \p PtrOp to \p Ty and can be reused when expanding
  /// ptrtoaddr.
  ///
  /// \param PtrOp Pointer value whose users are searched for a reusable cast.
  /// \param Ty Address-integer type of the desired ptrtoaddr result.
  /// \param DL Data layout used to validate the cast.
  /// \param Dominates Predicate that returns true if a candidate cast dominates
  ///        the expansion point.
  /// \return An existing cast that can be reused, or nullptr if none.
  LLVM_ABI static CastInst *
  findReusableCastForPtrToAddr(Value *PtrOp, Type *Ty, const DataLayout &DL,
                               function_ref<bool(const CastInst *)> Dominates);

  /// Insert code to compute \p SH at iterator \p I, casting to \p Ty if needed.
  ///
  /// The code is inserted into the specified block.
  ///
  /// \param SH SCEV expression to expand.
  /// \param Ty Desired result type, or nullptr to keep the natural type.
  /// \param I Insertion point within a basic block.
  /// \return The expanded value, cast to \p Ty when requested.
  LLVM_ABI Value *expandCodeFor(SCEVUse SH, Type *Ty, BasicBlock::iterator I);
  /// Insert code to compute \p SH before instruction \p I, casting to \p Ty if
  /// needed.
  ///
  /// \param SH SCEV expression to expand.
  /// \param Ty Desired result type, or nullptr to keep the natural type.
  /// \param I Instruction before which the expansion is inserted.
  /// \return The expanded value, cast to \p Ty when requested.
  Value *expandCodeFor(SCEVUse SH, Type *Ty, Instruction *I) {
    return expandCodeFor(SH, Ty, I->getIterator());
  }

  /// Expand \p SH at the current insertion point, casting to \p Ty if needed.
  ///
  /// The code is inserted into the SCEVExpander's current insertion point. If a
  /// type is specified, the result will be expanded to have that type, with a
  /// cast if necessary.
  ///
  /// \param SH SCEV expression to expand.
  /// \param Ty Desired result type, or nullptr to keep the natural type.
  /// \return The expanded value, cast to \p Ty when requested.
  LLVM_ABI Value *expandCodeFor(SCEVUse SH, Type *Ty = nullptr);

  /// Expand \p Pred to an i1 value inserted at \p Loc.
  ///
  /// The result will have a value of 0 when the predicate is false and 1
  /// otherwise.
  ///
  /// \param Pred Predicate to evaluate.
  /// \param Loc Instruction at which the predicate code is inserted.
  /// \return An i1 value that is true when the predicate holds.
  LLVM_ABI Value *expandCodeForPredicate(const SCEVPredicate *Pred,
                                         Instruction *Loc);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVComparePredicate.
  ///
  /// \param Pred Compare predicate to expand.
  /// \param Loc Instruction at which the compare is inserted.
  /// \return An i1 value that is true when the compare predicate holds.
  LLVM_ABI Value *expandComparePredicate(const SCEVComparePredicate *Pred,
                                         Instruction *Loc);

  /// Generates code that evaluates if the \p AR expression will overflow.
  ///
  /// \param AR Add-recurrence expression to test for overflow.
  /// \param Loc Instruction at which the overflow check is inserted.
  /// \param Signed Whether to check signed rather than unsigned overflow.
  /// \return An i1 value that is true when \p AR would overflow.
  LLVM_ABI Value *generateOverflowCheck(const SCEVAddRecExpr *AR,
                                        Instruction *Loc, bool Signed);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVWrapPredicate.
  ///
  /// \param P Wrap predicate to expand.
  /// \param Loc Instruction at which the wrap check is inserted.
  /// \return An i1 value that is true when the wrap predicate holds.
  LLVM_ABI Value *expandWrapPredicate(const SCEVWrapPredicate *P,
                                      Instruction *Loc);

  /// A specialized variant of expandCodeForPredicate, handling the case when
  /// we are expanding code for a SCEVUnionPredicate.
  ///
  /// \param Pred Union predicate to expand.
  /// \param Loc Instruction at which the union predicate is inserted.
  /// \return An i1 value that is true when the union predicate holds.
  LLVM_ABI Value *expandUnionPredicate(const SCEVUnionPredicate *Pred,
                                       Instruction *Loc);

  /// Set the current IV increment loop and position.
  ///
  /// \param L Loop whose IV increments use \p Pos.
  /// \param Pos Instruction at which IV increments are inserted.
  void setIVIncInsertPos(const Loop *L, Instruction *Pos) {
    assert(!CanonicalMode &&
           "IV increment positions are not supported in CanonicalMode");
    IVIncInsertLoop = L;
    IVIncInsertPos = Pos;
  }

  /// Enable post-inc expansion for addrecs referring to the given
  /// loops. Post-inc expansion is only supported in non-canonical mode.
  ///
  /// \param L Set of loops for which addrecs expand in post-inc form.
  void setPostInc(const PostIncLoopSet &L) {
    assert(!CanonicalMode &&
           "Post-inc expansion is not supported in CanonicalMode");
    PostIncLoops = L;
  }

  /// Disable all post-inc expansion.
  void clearPostInc() {
    PostIncLoops.clear();

    // When we change the post-inc loop set, cached expansions may no
    // longer be valid.
    InsertedPostIncValues.clear();
  }

  /// Disable the behavior of expanding expressions in canonical form rather
  /// than in a more literal form. Non-canonical mode is useful for late
  /// optimization passes.
  void disableCanonicalMode() { CanonicalMode = false; }

  /// Enable strength-reduction mode for loop strength reduction.
  void enableLSRMode() { LSRMode = true; }

  /// Set the current insertion point to \p IP.
  ///
  /// This is useful if multiple calls to expandCodeFor() are going to be made
  /// with the same insert point and the insert point may be moved during one of
  /// the expansions (e.g. if the insert point is not a block terminator).
  ///
  /// \param IP Instruction that becomes the insertion point.
  void setInsertPoint(Instruction *IP) {
    assert(IP);
    Builder.SetInsertPoint(IP);
  }

  /// Set the current insertion point to iterator \p IP.
  ///
  /// \param IP Iterator that becomes the insertion point.
  void setInsertPoint(BasicBlock::iterator IP) {
    Builder.SetInsertPoint(IP->getParent(), IP);
  }

  /// Clear the current insertion point. This is useful if the instruction
  /// that had been serving as the insertion point may have been deleted.
  void clearInsertPoint() { Builder.ClearInsertionPoint(); }

  /// Set location information used by debugging information.
  ///
  /// \param L Debug location applied to subsequently inserted instructions.
  void SetCurrentDebugLocation(DebugLoc L) {
    Builder.SetCurrentDebugLocation(std::move(L));
  }

  /// Get location information used by debugging information.
  ///
  /// \return The debug location applied to subsequently inserted instructions.
  DebugLoc getCurrentDebugLocation() const {
    return Builder.getCurrentDebugLocation();
  }

  /// Return true if the specified instruction was inserted by the code rewriter.
  ///
  /// If so, the client should not modify the instruction. Note that this also
  /// includes instructions re-used during expansion.
  ///
  /// \param I Instruction to test for expander ownership.
  /// \return True if \p I was inserted or reused by the expander.
  bool isInsertedInstruction(Instruction *I) const {
    return InsertedValues.count(I) || InsertedPostIncValues.count(I);
  }

  /// Record \p PN as completing an IV chain so it can be reused.
  ///
  /// \param PN PHI node that completes an IV chain.
  void setChainedPhi(PHINode *PN) { ChainedPhis.insert(PN); }

  /// Determine whether there is an existing expansion of S that can be reused.
  /// This is used to check whether S can be expanded cheaply.
  ///
  /// L is a hint which tells in which loop to look for the suitable value.
  ///
  /// Note that this function does not perform an exhaustive search. I.e if it
  /// didn't find any value it does not mean that there is no such value.
  ///
  /// \param S SCEV expression to look up.
  /// \param At Instruction near which a reusable expansion is sought.
  /// \param L Loop hint guiding where to search for a suitable value.
  /// \return True if a related existing expansion of \p S was found.
  LLVM_ABI bool hasRelatedExistingExpansion(const SCEV *S,
                                            const Instruction *At, Loop *L);

  /// Returns a suitable insert point after \p I, that dominates \p
  /// MustDominate. Skips instructions inserted by the expander.
  ///
  /// \param I Instruction after which insertion is considered.
  /// \param MustDominate Instruction that the chosen insert point must dominate.
  /// \return A suitable insert point after \p I that dominates \p MustDominate.
  LLVM_ABI BasicBlock::iterator
  findInsertPointAfter(Instruction *I, Instruction *MustDominate) const;

  /// Remove inserted instructions that are dead, e.g. due to InstSimplifyFolder
  /// simplifications. \p Root is assumed to be used and won't be removed.
  ///
  /// \param Root Value that is considered live and must not be removed.
  LLVM_ABI void eraseDeadInstructions(Value *Root);

private:
  LLVMContext &getContext() const { return SE.getContext(); }

  /// Recursive helper function for isHighCostExpansion.
  LLVM_ABI bool
  isHighCostExpansionHelper(const SCEVOperand &WorkItem, Loop *L,
                            const Instruction &At, InstructionCost &Cost,
                            unsigned Budget, const TargetTransformInfo &TTI,
                            SmallPtrSetImpl<const SCEV *> &Processed,
                            SmallVectorImpl<SCEVOperand> &Worklist);

  /// Insert the specified binary operator, doing a small amount of work to
  /// avoid inserting an obviously redundant operation, and hoisting to an
  /// outer loop when the opportunity is there and it is safe.
  Value *InsertBinop(Instruction::BinaryOps Opcode, Value *LHS, Value *RHS,
                     SCEV::NoWrapFlags Flags, bool IsSafeToHoist);

  /// We want to cast \p V. What would be the best place for such a cast?
  BasicBlock::iterator GetOptimalInsertionPointForCastOf(Value *V) const;

  /// Arrange for there to be a cast of V to Ty at IP, reusing an existing
  /// cast if a suitable one exists, moving an existing cast if a suitable one
  /// exists but isn't in the right place, or creating a new one.
  Value *ReuseOrCreateCast(Value *V, Type *Ty, Instruction::CastOps Op,
                           BasicBlock::iterator IP);

  /// Insert a cast of V to the specified type, which must be possible with a
  /// noop cast, doing what we can to share the casts.
  Value *InsertNoopCastOfTo(Value *V, Type *Ty);

  /// Expand a SCEVAddExpr with a pointer type into a GEP instead of using
  /// ptrtoint+arithmetic+inttoptr.
  Value *expandAddToGEP(const SCEV *Op, Value *V, SCEV::NoWrapFlags Flags);

  /// Find a previous Value in ExprValueMap for expand.
  /// DropPoisonGeneratingInsts is populated with instructions for which
  /// poison-generating flags must be dropped if the value is reused.
  Value *FindValueInExprValueMap(
      SCEVUse S, const Instruction *InsertPt,
      SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts);

  /// Like FindValueInExprValueMap, but on a successful lookup also drops the
  /// poison-generating flags that reusing the value requires.
  Value *findExistingExpansionAndDropPoisonFlags(SCEVUse S,
                                                 const Instruction *InsertPt);

  LLVM_ABI Value *expand(SCEVUse S);
  Value *expand(SCEVUse S, BasicBlock::iterator I) {
    setInsertPoint(I);
    return expand(S);
  }
  Value *expand(SCEVUse S, Instruction *I) {
    setInsertPoint(I);
    return expand(S);
  }

  /// Determine the most "relevant" loop for the given SCEV.
  const Loop *getRelevantLoop(const SCEV *);

  Value *expandMinMaxExpr(SCEVUseT<const SCEVNAryExpr *> S,
                          Intrinsic::ID IntrinID, Twine Name,
                          bool IsSequential = false);

  Value *visitConstant(SCEVUseT<const SCEVConstant *> S) {
    return S->getValue();
  }

  Value *visitVScale(SCEVUseT<const SCEVVScale *> S);

  Value *visitPtrToAddrExpr(SCEVUseT<const SCEVPtrToAddrExpr *> S);

  Value *visitTruncateExpr(SCEVUseT<const SCEVTruncateExpr *> S);

  Value *visitZeroExtendExpr(SCEVUseT<const SCEVZeroExtendExpr *> S);

  Value *visitSignExtendExpr(SCEVUseT<const SCEVSignExtendExpr *> S);

  Value *visitAddExpr(SCEVUseT<const SCEVAddExpr *> S);

  Value *visitMulExpr(SCEVUseT<const SCEVMulExpr *> S);

  Value *visitUDivExpr(SCEVUseT<const SCEVUDivExpr *> S);

  Value *visitAddRecExpr(SCEVUseT<const SCEVAddRecExpr *> S);

  Value *visitSMaxExpr(SCEVUseT<const SCEVSMaxExpr *> S);

  Value *visitUMaxExpr(SCEVUseT<const SCEVUMaxExpr *> S);

  Value *visitSMinExpr(SCEVUseT<const SCEVSMinExpr *> S);

  Value *visitUMinExpr(SCEVUseT<const SCEVUMinExpr *> S);

  Value *visitSequentialUMinExpr(SCEVUseT<const SCEVSequentialUMinExpr *> S);

  Value *visitUnknown(SCEVUseT<const SCEVUnknown *> S) { return S->getValue(); }

  LLVM_ABI void rememberInstruction(Value *I);

  void rememberFlags(Instruction *I);

  bool isNormalAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

  bool isExpandedAddRecExprPHI(PHINode *PN, Instruction *IncV, const Loop *L);

  Value *tryToReuseLCSSAPhi(SCEVUseT<const SCEVAddRecExpr *> S);
  Value *expandAddRecExprLiterally(SCEVUseT<const SCEVAddRecExpr *> S);
  PHINode *getAddRecExprPHILiterally(const SCEVAddRecExpr *Normalized,
                                     const Loop *L, Type *&TruncTy,
                                     bool &InvertStep);
  Value *expandIVInc(PHINode *PN, Value *StepV, const Loop *L,
                     bool useSubtract);

  void fixupInsertPoints(Instruction *I);

  /// Create LCSSA PHIs for \p V, if it is required for uses at the Builder's
  /// current insertion point.
  Value *fixupLCSSAFormFor(Value *V);

  /// Replace congruent phi increments with their most canonical representative.
  /// May swap \p Phi and \p OrigPhi, if \p Phi is more canonical, due to its
  /// increment.
  void replaceCongruentIVInc(PHINode *&Phi, PHINode *&OrigPhi, Loop *L,
                             const DominatorTree *DT,
                             SmallVectorImpl<WeakTrackingVH> &DeadInsts);
};

/// Helper to remove instructions inserted during SCEV expansion, unless they
/// are marked as used.
class SCEVExpanderCleaner {
  SCEVExpander &Expander;

  /// Indicates whether the result of the expansion is used. If false, the
  /// instructions added during expansion are removed.
  bool ResultUsed;

public:
  /// Create a cleaner that removes unused expansions from \p Expander.
  ///
  /// \param Expander Expander whose inserted instructions may be cleaned up.
  SCEVExpanderCleaner(SCEVExpander &Expander)
      : Expander(Expander), ResultUsed(false) {}

  /// Destroy the cleaner and remove unused expansions unless marked used.
  ~SCEVExpanderCleaner() { cleanup(); }

  /// Indicate that the result of the expansion is used.
  void markResultUsed() { ResultUsed = true; }

  /// Remove unused instructions inserted during SCEV expansion.
  LLVM_ABI void cleanup();
};
} // namespace llvm

#endif
