//===- llvm/Analysis/LoopInfo.h - Natural Loop Calculator -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a GenericLoopInfo instantiation for LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPINFO_H
#define LLVM_ANALYSIS_LOOPINFO_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericLoopInfo.h"
#include <optional>
#include <utility>

namespace llvm {

class DominatorTree;
class InductionDescriptor;
class LoopInfo;
class Loop;
class MemorySSAUpdater;
class ScalarEvolution;
class raw_ostream;

// Implementation in Support/GenericLoopInfoImpl.h
/// Explicit instantiation of LoopBase for LLVM IR BasicBlock and Loop.
extern template class LLVM_TEMPLATE_ABI LoopBase<BasicBlock, Loop>;

/// Represents a single loop in the control flow graph.  Note that not all SCCs
/// in the CFG are necessarily loops.
class LLVM_ABI Loop : public LoopBase<BasicBlock, Loop> {
public:
  /// A range representing the start and end location of a loop.
  class LocRange {
    DebugLoc Start;
    DebugLoc End;

  public:
    /// Construct an empty location range.
    LocRange() = default;
    /// Construct a range with the same start and end location.
    /// @param Start Debug location used for both ends of the range.
    LocRange(DebugLoc Start) : Start(Start), End(Start) {}
    /// Construct a range from distinct start and end locations.
    /// @param Start Debug location of the start of the range.
    /// @param End Debug location of the end of the range.
    LocRange(DebugLoc Start, DebugLoc End)
        : Start(std::move(Start)), End(std::move(End)) {}

    /// Return the start debug location of this range.
    /// @return The start debug location.
    const DebugLoc &getStart() const { return Start; }
    /// Return the end debug location of this range.
    /// @return The end debug location.
    const DebugLoc &getEnd() const { return End; }

    /// Check for null.
    ///
    /// @return True if both the start and end debug locations are set.
    explicit operator bool() const { return Start && End; }
  };

  /// Return true if the specified value is loop invariant.
  /// @param V Value to test for loop invariance.
  /// @return True if \p V is loop-invariant.
  bool isLoopInvariant(const Value *V) const;

  /// Return true if all the operands of the specified instruction are loop
  /// invariant.
  /// @param I Instruction whose operands are tested.
  /// @return True if every operand of \p I is loop-invariant.
  bool hasLoopInvariantOperands(const Instruction *I) const;

  /// Make \p V loop-invariant by hoisting it when possible.
  ///
  /// If the given value is an instruction inside of the loop and it can be
  /// hoisted, do so to make it trivially loop-invariant.
  /// Return true if \c V is already loop-invariant, and false if \c V can't
  /// be made loop-invariant. If \c V is made loop-invariant, \c Changed is
  /// set to true. This function can be used as a slightly more aggressive
  /// replacement for isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  /// @param V Value to make loop-invariant.
  /// @param Changed Set to true if \p V is hoisted.
  /// @param InsertPt Optional hoist insertion point; null uses the preheader.
  /// @param MSSAU Optional MemorySSA updater for the hoist.
  /// @param SE Optional ScalarEvolution to invalidate after hoisting.
  /// @return True if \p V is or was made loop-invariant; false if it cannot.
  bool makeLoopInvariant(Value *V, bool &Changed,
                         Instruction *InsertPt = nullptr,
                         MemorySSAUpdater *MSSAU = nullptr,
                         ScalarEvolution *SE = nullptr) const;

  /// Make \p I loop-invariant by hoisting it when possible.
  ///
  /// If the given instruction is inside of the loop and it can be hoisted, do
  /// so to make it trivially loop-invariant.
  /// Return true if \c I is already loop-invariant, and false if \c I can't
  /// be made loop-invariant. If \c I is made loop-invariant, \c Changed is
  /// set to true. This function can be used as a slightly more aggressive
  /// replacement for isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  /// @param I Instruction to make loop-invariant.
  /// @param Changed Set to true if \p I is hoisted.
  /// @param InsertPt Optional hoist insertion point; null uses the preheader.
  /// @param MSSAU Optional MemorySSA updater for the hoist.
  /// @param SE Optional ScalarEvolution to invalidate after hoisting.
  /// @return True if \p I is or was made loop-invariant; false if it cannot.
  bool makeLoopInvariant(Instruction *I, bool &Changed,
                         Instruction *InsertPt = nullptr,
                         MemorySSAUpdater *MSSAU = nullptr,
                         ScalarEvolution *SE = nullptr) const;

  /// Return the canonical induction variable PHI, if one exists.
  ///
  /// Check to see if the loop has a canonical induction variable: an integer
  /// recurrence that starts at 0 and increments by one each time through the
  /// loop. If so, return the phi node that corresponds to it.
  ///
  /// The IndVarSimplify pass transforms loops to have a canonical induction
  /// variable.
  /// @return The canonical induction PHI, or nullptr if none exists.
  PHINode *getCanonicalInductionVariable() const;

  /// Get the latch condition instruction.
  /// @return The latch compare instruction, or nullptr if none exists.
  ICmpInst *getLatchCmpInst() const;

  /// Obtain the unique incoming and back edge. Return false if they are
  /// non-unique or the loop is dead; otherwise, return true.
  /// @param Incoming Set to the unique non-latch predecessor of the header.
  /// @param Backedge Set to the unique latch/backedge block.
  /// @return True if unique incoming and back edges were found.
  bool getIncomingAndBackEdge(BasicBlock *&Incoming,
                              BasicBlock *&Backedge) const;

  /// Utilities for the induction variable bounds of a loop.
  ///
  /// Below are some utilities to get the loop guard, loop bounds and induction
  /// variable, and to check if a given phinode is an auxiliary induction
  /// variable, if the loop is guarded, and if the loop is canonical.
  ///
  /// Here is an example:
  /// \code
  /// for (int i = lb; i < ub; i+=step)
  ///   <loop body>
  /// --- pseudo LLVMIR ---
  /// beforeloop:
  ///   guardcmp = (lb < ub)
  ///   if (guardcmp) goto preheader; else goto afterloop
  /// preheader:
  /// loop:
  ///   i_1 = phi[{lb, preheader}, {i_2, latch}]
  ///   <loop body>
  ///   i_2 = i_1 + step
  /// latch:
  ///   cmp = (i_2 < ub)
  ///   if (cmp) goto loop
  /// exit:
  /// afterloop:
  /// \endcode
  ///
  /// - getBounds
  ///   - getInitialIVValue      --> lb
  ///   - getStepInst            --> i_2 = i_1 + step
  ///   - getStepValue           --> step
  ///   - getFinalIVValue        --> ub
  ///   - getCanonicalPredicate  --> '<'
  ///   - getDirection           --> Increasing
  ///
  /// - getInductionVariable            --> i_1
  /// - isAuxiliaryInductionVariable(x) --> true if x == i_1
  /// - getLoopGuardBranch()
  ///                 --> `if (guardcmp) goto preheader; else goto afterloop`
  /// - isGuarded()                     --> true
  /// - isCanonical                     --> false
  struct LoopBounds {
    /// Return LoopBounds for \p IndVar when its initial, step, and final values
    /// can be found; otherwise return std::nullopt.
    ///
    /// Return the LoopBounds object if
    /// - the given \p IndVar is an induction variable
    /// - the initial value of the induction variable can be found
    /// - the step instruction of the induction variable can be found
    /// - the final value of the induction variable can be found
    ///
    /// Else std::nullopt.
    /// @param L Loop that owns \p IndVar.
    /// @param IndVar Candidate induction PHI in \p L.
    /// @param SE ScalarEvolution used to analyze the induction variable.
    /// @return LoopBounds for \p IndVar, or std::nullopt if values are missing.
    LLVM_ABI static std::optional<Loop::LoopBounds>
    getBounds(const Loop &L, PHINode &IndVar, ScalarEvolution &SE);

    /// Get the initial value of the loop induction variable.
    /// @return The initial value of the induction variable.
    Value &getInitialIVValue() const { return InitialIVValue; }

    /// Get the instruction that updates the loop induction variable.
    /// @return The instruction that steps the induction variable.
    Instruction &getStepInst() const { return StepInst; }

    /// Get the step that the loop induction variable gets updated by in each
    /// loop iteration. Return nullptr if not found.
    /// @return The step value, or nullptr if not found.
    Value *getStepValue() const { return StepValue; }

    /// Get the final value of the loop induction variable.
    /// @return The final value of the induction variable.
    Value &getFinalIVValue() const { return FinalIVValue; }

    /// Return the canonical predicate for the latch compare instruction, if
    /// able to be calcuated. Else BAD_ICMP_PREDICATE.
    ///
    /// A predicate is considered as canonical if requirements below are all
    /// satisfied:
    /// 1. The first successor of the latch branch is the loop header
    ///    If not, inverse the predicate.
    /// 2. One of the operands of the latch comparison is StepInst
    ///    If not, and
    ///    - if the current calcuated predicate is not ne or eq, flip the
    ///      predicate.
    ///    - else if the loop is increasing, return slt
    ///      (notice that it is safe to change from ne or eq to sign compare)
    ///    - else if the loop is decreasing, return sgt
    ///      (notice that it is safe to change from ne or eq to sign compare)
    ///
    /// Here is an example when both (1) and (2) are not satisfied:
    /// \code
    /// loop.header:
    ///  %iv = phi [%initialiv, %loop.preheader], [%inc, %loop.header]
    ///  %inc = add %iv, %step
    ///  %cmp = slt %iv, %finaliv
    ///  br %cmp, %loop.exit, %loop.header
    /// loop.exit:
    /// \endcode
    /// - The second successor of the latch branch is the loop header instead
    ///   of the first successor (slt -> sge)
    /// - The first operand of the latch comparison (%cmp) is the IndVar (%iv)
    ///   instead of the StepInst (%inc) (sge -> sgt)
    ///
    /// The predicate would be sgt if both (1) and (2) are satisfied.
    /// getCanonicalPredicate() returns sgt for this example.
    /// Note: The IR is not changed.
    /// @return The canonical latch compare predicate, or BAD_ICMP_PREDICATE.
    LLVM_ABI ICmpInst::Predicate getCanonicalPredicate() const;

    /// Direction of the induction variable across loop iterations.
    ///
    /// Examples:
    /// - for (int i = 0; i < ub; ++i)  --> Increasing
    /// - for (int i = ub; i > 0; --i)  --> Decreasing
    /// - for (int i = x; i != y; i+=z) --> Unknown
    enum class Direction {
      /// Induction variable increases each iteration.
      Increasing,
      /// Induction variable decreases each iteration.
      Decreasing,
      /// Induction direction cannot be determined.
      Unknown
    };

    /// Get the direction of the loop.
    /// @return Whether the induction variable increases, decreases, or is unknown.
    LLVM_ABI Direction getDirection() const;

  private:
    LoopBounds(const Loop &Loop, Value &I, Instruction &SI, Value *SV, Value &F,
               ScalarEvolution &SE)
        : L(Loop), InitialIVValue(I), StepInst(SI), StepValue(SV),
          FinalIVValue(F), SE(SE) {}

    const Loop &L;

    // The initial value of the loop induction variable
    Value &InitialIVValue;

    // The instruction that updates the loop induction variable
    Instruction &StepInst;

    // The value that the loop induction variable gets updated by in each loop
    // iteration
    Value *StepValue;

    // The final value of the loop induction variable
    Value &FinalIVValue;

    ScalarEvolution &SE;
  };

  /// Return the struct LoopBounds collected if all struct members are found,
  /// else std::nullopt.
  /// @param SE ScalarEvolution used to collect induction bounds.
  /// @return LoopBounds when complete, otherwise std::nullopt.
  std::optional<LoopBounds> getBounds(ScalarEvolution &SE) const;

  /// Return the loop induction variable if found, else return nullptr.
  ///
  /// An instruction is considered as the loop induction variable if
  /// - it is an induction variable of the loop; and
  /// - it is used to determine the condition of the branch in the loop latch
  ///
  /// Note: the induction variable doesn't need to be canonical, i.e. starts at
  /// zero and increments by one each time through the loop (but it can be).
  /// @param SE ScalarEvolution used to recognize the induction variable.
  /// @return The induction PHI, or nullptr if none is found.
  PHINode *getInductionVariable(ScalarEvolution &SE) const;

  /// Get the loop induction descriptor for the loop induction variable. Return
  /// true if the loop induction variable is found.
  /// @param SE ScalarEvolution used to recognize the induction variable.
  /// @param IndDesc Filled with the induction descriptor on success.
  /// @return True if the induction variable was found and \p IndDesc filled.
  bool getInductionDescriptor(ScalarEvolution &SE,
                              InductionDescriptor &IndDesc) const;

  /// Return true if \p AuxIndVar is an auxiliary induction variable of this
  /// loop.
  ///
  /// Return true if the given PHINode \p AuxIndVar is
  /// - in the loop header
  /// - not used outside of the loop
  /// - incremented by a loop invariant step for each loop iteration
  /// - step instruction opcode should be add or sub
  /// Note: auxiliary induction variable is not required to be used in the
  ///       conditional branch in the loop latch. (but it can be)
  /// @param AuxIndVar Candidate auxiliary induction PHI.
  /// @param SE ScalarEvolution used to analyze the induction step.
  /// @return True if \p AuxIndVar is an auxiliary induction variable.
  bool isAuxiliaryInductionVariable(PHINode &AuxIndVar,
                                    ScalarEvolution &SE) const;

  /// Return the loop guard branch, if it exists.
  ///
  /// This currently only works on simplified loop, as it requires a preheader
  /// and a latch to identify the guard. It will work on loops of the form:
  /// \code
  /// GuardBB:
  ///   br cond1, Preheader, ExitSucc <== GuardBranch
  /// Preheader:
  ///   br Header
  /// Header:
  ///  ...
  ///   br Latch
  /// Latch:
  ///   br cond2, Header, ExitBlock
  /// ExitBlock:
  ///   br ExitSucc
  /// ExitSucc:
  /// \endcode
  /// @return The guard branch instruction, or nullptr if none exists.
  CondBrInst *getLoopGuardBranch() const;

  /// Return true iff the loop is
  /// - in simplify rotated form, and
  /// - guarded by a loop guard branch.
  /// @return True if the loop has a guard branch.
  bool isGuarded() const { return (getLoopGuardBranch() != nullptr); }

  /// Return true if the loop is in rotated form.
  ///
  /// This does not check if the loop was rotated by loop rotation, instead it
  /// only checks if the loop is in rotated form (has a valid latch that exists
  /// the loop).
  /// @return True if the loop latch exits the loop.
  bool isRotatedForm() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    BasicBlock *Latch = getLoopLatch();
    return Latch && isLoopExiting(Latch);
  }

  /// Return true if the loop induction variable starts at zero and increments
  /// by one each time through the loop.
  /// @param SE ScalarEvolution used to analyze the induction variable.
  /// @return True if the induction variable is canonical.
  bool isCanonical(ScalarEvolution &SE) const;

  /// Return true if the Loop is in LCSSA form. If \p IgnoreTokens is set to
  /// true, token-like values defined inside loop are allowed to violate LCSSA
  /// form.
  /// @param DT Dominator tree of the function containing this loop.
  /// @param IgnoreTokens When true, token values may violate LCSSA.
  /// @return True if this loop is in LCSSA form.
  bool isLCSSAForm(const DominatorTree &DT, bool IgnoreTokens = true) const;

  /// Return true if this Loop and all inner subloops are in LCSSA form.
  ///
  /// If \p IgnoreTokens is set to true, token-like values defined inside loop
  /// are allowed to violate LCSSA form.
  /// @param DT Dominator tree of the function containing this loop.
  /// @param LI LoopInfo for the function containing this loop.
  /// @param IgnoreTokens When true, token values may violate LCSSA.
  /// @return True if this loop and all subloops are in LCSSA form.
  bool isRecursivelyLCSSAForm(const DominatorTree &DT, const LoopInfo &LI,
                              bool IgnoreTokens = true) const;

  /// Return true if the Loop is in the form that the LoopSimplify form
  /// transforms loops to, which is sometimes called normal form.
  /// @return True if the loop is in LoopSimplify form.
  bool isLoopSimplifyForm() const;

  /// Return true if the loop body is safe to clone in practice.
  /// @return True if the loop body is safe to clone.
  bool isSafeToClone() const;

  /// Return true if the loop body is safe to clone under conditional control.
  ///
  /// Like `isSafeToClone`, but for transformations where the cloned loop
  /// bodies may run conditionally. This additionally checks that we may form
  /// phis for all values that are live-out from the loop (in particular that
  /// no token-like values are live-out) and that there are no convergent calls
  /// (which must not gain new control dependencies).
  /// @param DT Dominator tree of the function containing this loop.
  /// @return True if the loop is safe to clone under conditional control.
  bool isSafeToCloneConditionally(const DominatorTree &DT) const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// A parallel loop can be assumed to not contain any dependencies between
  /// iterations by the compiler. That is, any loop-carried dependency checking
  /// can be skipped completely when parallelizing the loop on the target
  /// machine. Thus, if the parallel loop information originates from the
  /// programmer, e.g. via the OpenMP parallel for pragma, it is the
  /// programmer's responsibility to ensure there are no loop-carried
  /// dependencies. The final execution order of the instructions across
  /// iterations is not guaranteed, thus, the end result might or might not
  /// implement actual concurrent execution of instructions across multiple
  /// iterations.
  /// @return True if the loop is annotated as parallel.
  bool isAnnotatedParallel() const;

  /// Return the llvm.loop loop id metadata node for this loop if it is present.
  ///
  /// If this loop contains the same llvm.loop metadata on each branch to the
  /// header then the node is returned. If any latch instruction does not
  /// contain llvm.loop or if multiple latches contain different nodes then
  /// 0 is returned.
  /// @return The loop-ID metadata node, or nullptr if absent or inconsistent.
  MDNode *getLoopID() const;
  /// Set the llvm.loop loop id metadata for this loop.
  ///
  /// The LoopID metadata node will be added to each terminator instruction in
  /// the loop that branches to the loop header.
  ///
  /// The LoopID metadata node should have one or more operands and the first
  /// operand should be the node itself.
  /// @param LoopID Loop-ID metadata node to attach to this loop.
  void setLoopID(MDNode *LoopID) const;

  /// Add llvm.loop.unroll.disable to this loop's loop id metadata.
  ///
  /// Remove existing unroll metadata and add unroll disable metadata to
  /// indicate the loop has already been unrolled.  This prevents a loop
  /// from being unrolled more than is directed by a pragma if the loop
  /// unrolling pass is run more than once (which it generally is).
  void setLoopAlreadyUnrolled();

  /// Add llvm.loop.mustprogress to this loop's loop id metadata.
  void setLoopMustProgress();

  /// Add a string-only metadata attribute to this loop's loop-ID node.
  ///
  /// Creates an MDNode containing just \p Name (no value operand) and appends
  /// it to the loop metadata via makePostTransformationMetadata. Any existing
  /// attributes whose key starts with one of \p RemovePrefixes are stripped
  /// first.
  /// @param Name String attribute key to append to the loop-ID metadata.
  /// @param RemovePrefixes Attribute key prefixes to strip before appending.
  void addStringLoopAttribute(StringRef Name,
                              ArrayRef<StringRef> RemovePrefixes = {}) const;

  /// Add an integer metadata attribute to this loop's loop-ID node.
  ///
  /// Creates an MDNode of the form { Name, ConstantInt(Value) } and appends
  /// it to the loop metadata via makePostTransformationMetadata. Any existing
  /// attributes whose key starts with one of \p RemovePrefixes are stripped
  /// first.
  /// @param Name Integer attribute key to append to the loop-ID metadata.
  /// @param Value Integer value paired with \p Name.
  /// @param RemovePrefixes Attribute key prefixes to strip before appending.
  void addIntLoopAttribute(StringRef Name, unsigned Value,
                           ArrayRef<StringRef> RemovePrefixes = {}) const;

  /// Dump this loop to stderr for debugging.
  void dump() const;
  /// Dump this loop and nested structure verbosely to stderr.
  void dumpVerbose() const;

  /// Return the debug location of the start of this loop.
  ///
  /// This looks for a BB terminating instruction with a known debug location by
  /// looking at the preheader and header blocks. If it cannot find a
  /// terminating instruction with location information, it returns an unknown
  /// location.
  /// @return Debug location of the start of this loop, or an unknown location.
  DebugLoc getStartLoc() const;

  /// Return the source code span of the loop.
  /// @return Location range covering the start and end of this loop.
  LocRange getLocRange() const;

  /// Return a debug string for this loop's source location.
  ///
  /// Contains the file name and line number if present, otherwise the module
  /// name. Meant to be used for debug printing within LLVM_DEBUG.
  /// @return A string describing this loop's source location.
  std::string getLocStr() const;

  /// Return the name of this loop's header block, or a placeholder.
  /// @return The header block name, or "<unnamed loop>" if none.
  StringRef getName() const {
    if (BasicBlock *Header = getHeader())
      if (Header->hasName())
        return Header->getName();
    return "<unnamed loop>";
  }

private:
  Loop() = default;

  friend class LoopInfoBase<BasicBlock, Loop>;
  friend class LoopBase<BasicBlock, Loop>;
  ~Loop() = default;
};

// Implementation in Support/GenericLoopInfoImpl.h
extern template class LLVM_TEMPLATE_ABI LoopInfoBase<BasicBlock, Loop>;

/// Analysis that identifies natural loops in a function's CFG.
class LoopInfo : public LoopInfoBase<BasicBlock, Loop> {
  typedef LoopInfoBase<BasicBlock, Loop> BaseT;

  friend class LoopBase<BasicBlock, Loop>;

  /// Deleted copy assignment; LoopInfo is not copyable.
  /// @param Unused Unused right-hand side; copying is not supported.
  void operator=(const LoopInfo &) = delete;
  /// Deleted copy constructor; LoopInfo is not copyable.
  /// @param Unused Unused copy source; copying is not supported.
  LoopInfo(const LoopInfo &) = delete;

public:
  /// Construct an empty LoopInfo.
  LoopInfo() = default;
  /// Construct LoopInfo by analyzing \p DomTree.
  /// @param DomTree Dominator tree used to discover natural loops.
  LLVM_ABI explicit LoopInfo(
      const DominatorTreeBase<BasicBlock, false> &DomTree);

  /// Move-construct LoopInfo from \p Arg.
  /// @param Arg LoopInfo to move from.
  LoopInfo(LoopInfo &&Arg) : BaseT(std::move(static_cast<BaseT &>(Arg))) {}
  /// Move-assign LoopInfo from \p RHS.
  /// @param RHS LoopInfo to move from.
  /// @return A reference to this LoopInfo after the move.
  LoopInfo &operator=(LoopInfo &&RHS) {
    BaseT::operator=(std::move(static_cast<BaseT &>(RHS)));
    return *this;
  }

  /// Handle invalidation explicitly.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  // Most of the public interface is provided via LoopInfoBase.

  /// Update LoopInfo after removing the last backedge from a loop.
  ///
  /// This updates the loop forest and parent loops for each block so that \c L
  /// is no longer referenced, but does not actually delete \c L immediately.
  /// The pointer will remain valid until this LoopInfo's memory is released.
  /// @param L Loop that no longer has a backedge and should be erased.
  LLVM_ABI void erase(Loop *L);

  /// Returns true if replacing From with To everywhere is guaranteed to
  /// preserve LCSSA form.
  /// @param From Instruction being replaced.
  /// @param To Replacement value.
  /// @return True if the replacement is guaranteed to preserve LCSSA.
  bool replacementPreservesLCSSAForm(Instruction *From, Value *To) {
    // Preserving LCSSA form is only problematic if the replacing value is an
    // instruction.
    Instruction *I = dyn_cast<Instruction>(To);
    if (!I)
      return true;
    // If both instructions are defined in the same basic block then replacement
    // cannot break LCSSA form.
    if (I->getParent() == From->getParent())
      return true;
    // If the instruction is not defined in a loop then it can safely replace
    // anything.
    Loop *ToLoop = getLoopFor(I->getParent());
    if (!ToLoop)
      return true;
    // If the replacing instruction is defined in the same loop as the original
    // instruction, or in a loop that contains it as an inner loop, then using
    // it as a replacement will not break LCSSA form.
    return ToLoop->contains(getLoopFor(From->getParent()));
  }

  /// Checks if moving a specific instruction can break LCSSA in any loop.
  ///
  /// Return true if moving \p Inst to before \p NewLoc will break LCSSA,
  /// assuming that the function containing \p Inst and \p NewLoc is currently
  /// in LCSSA form.
  /// @param Inst Instruction being moved.
  /// @param NewLoc Insertion point before which \p Inst would be placed.
  /// @return True if the move preserves LCSSA form.
  bool movementPreservesLCSSAForm(Instruction *Inst, Instruction *NewLoc) {
    assert(Inst->getFunction() == NewLoc->getFunction() &&
           "Can't reason about IPO!");

    auto *OldBB = Inst->getParent();
    auto *NewBB = NewLoc->getParent();

    // Movement within the same loop does not break LCSSA (the equality check is
    // to avoid doing a hashtable lookup in case of intra-block movement).
    if (OldBB == NewBB)
      return true;

    auto *OldLoop = getLoopFor(OldBB);
    auto *NewLoop = getLoopFor(NewBB);

    if (OldLoop == NewLoop)
      return true;

    // Check if Outer contains Inner; with the null loop counting as the
    // "outermost" loop.
    auto Contains = [](const Loop *Outer, const Loop *Inner) {
      return !Outer || Outer->contains(Inner);
    };

    // To check that the movement of Inst to before NewLoc does not break LCSSA,
    // we need to check two sets of uses for possible LCSSA violations at
    // NewLoc: the users of NewInst, and the operands of NewInst.

    // If we know we're hoisting Inst out of an inner loop to an outer loop,
    // then the uses *of* Inst don't need to be checked.

    if (!Contains(NewLoop, OldLoop)) {
      for (Use &U : Inst->uses()) {
        auto *UI = cast<Instruction>(U.getUser());
        auto *UBB = isa<PHINode>(UI) ? cast<PHINode>(UI)->getIncomingBlock(U)
                                     : UI->getParent();
        if (UBB != NewBB && getLoopFor(UBB) != NewLoop)
          return false;
      }
    }

    // If we know we're sinking Inst from an outer loop into an inner loop, then
    // the *operands* of Inst don't need to be checked.

    if (!Contains(OldLoop, NewLoop)) {
      // See below on why we can't handle phi nodes here.
      if (isa<PHINode>(Inst))
        return false;

      for (Use &U : Inst->operands()) {
        auto *DefI = dyn_cast<Instruction>(U.get());
        if (!DefI)
          return false;

        // This would need adjustment if we allow Inst to be a phi node -- the
        // new use block won't simply be NewBB.

        auto *DefBlock = DefI->getParent();
        if (DefBlock != NewBB && getLoopFor(DefBlock) != NewLoop)
          return false;
      }
    }

    return true;
  }

  /// Return true if a use of \p V in \p ExitBB would need an LCSSA PHI.
  ///
  /// \p V is assumed to dominate \p ExitBB, and \p ExitBB must be the exit
  /// block of some loop. The IR is assumed to be in LCSSA form before the
  /// planned insertion.
  /// @param V Value that would be used in \p ExitBB.
  /// @param ExitBB Loop exit block where the new use would be inserted.
  /// @return True if the use would require an LCSSA PHI.
  LLVM_ABI bool
  wouldBeOutOfLoopUseRequiringLCSSA(const Value *V,
                                    const BasicBlock *ExitBB) const;
};

/// Enable verification of loop info.
///
/// The flag enables checks which are expensive and are disabled by default
/// unless the `EXPENSIVE_CHECKS` macro is defined.  The `-verify-loop-info`
/// flag allows the checks to be enabled selectively without re-compilation.
LLVM_ABI extern bool VerifyLoopInfo;

// Allow clients to walk the list of nested loops...
/// GraphTraits specialization for const Loop pointers.
template <> struct GraphTraits<const Loop *> {
  /// Graph node type for a const Loop.
  typedef const Loop *NodeRef;
  /// Iterator over child (nested) loops.
  typedef LoopInfo::iterator ChildIteratorType;

  /// Return \p L as the graph entry node.
  /// @param L Loop used as the entry node.
  /// @return \p L as the entry node.
  static NodeRef getEntryNode(const Loop *L) { return L; }
  /// Return the begin iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return Begin iterator over the nested loops of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return End iterator over the nested loops of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

/// GraphTraits specialization for mutable Loop pointers.
template <> struct GraphTraits<Loop *> {
  /// Graph node type for a Loop.
  typedef Loop *NodeRef;
  /// Iterator over child (nested) loops.
  typedef LoopInfo::iterator ChildIteratorType;

  /// Return \p L as the graph entry node.
  /// @param L Loop used as the entry node.
  /// @return \p L as the entry node.
  static NodeRef getEntryNode(Loop *L) { return L; }
  /// Return the begin iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return Begin iterator over the nested loops of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return End iterator over the nested loops of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

/// Analysis pass that exposes the \c LoopInfo for a function.
class LoopAnalysis : public AnalysisInfoMixin<LoopAnalysis> {
  friend AnalysisInfoMixin<LoopAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  typedef LoopInfo Result;

  /// Run the analysis over \p F and produce LoopInfo.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing DominatorTree.
  /// @return The computed LoopInfo for \p F.
  LLVM_ABI LoopInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c LoopAnalysis results.
class LoopPrinterPass : public RequiredPassInfoMixin<LoopPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed loop info.
  explicit LoopPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the LoopInfo for \p F and return all analyses preserved.
  /// @param F Function whose LoopInfo is printed.
  /// @param AM Function analysis manager providing LoopAnalysis.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c LoopAnalysis results.
struct LoopVerifierPass : public RequiredPassInfoMixin<LoopVerifierPass> {
  /// Verify the LoopInfo for \p F and return all analyses preserved.
  /// @param F Function whose LoopInfo is verified.
  /// @param AM Function analysis manager providing LoopAnalysis.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// The legacy pass manager's analysis pass to compute loop information.
class LLVM_ABI LoopInfoWrapperPass : public FunctionPass {
  LoopInfo LI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LoopInfo wrapper pass.
  LoopInfoWrapperPass();

  /// Return the LoopInfo computed by this pass.
  /// @return The LoopInfo owned by this pass.
  LoopInfo &getLoopInfo() { return LI; }
  /// Return the LoopInfo computed by this pass.
  /// @return The LoopInfo owned by this pass.
  const LoopInfo &getLoopInfo() const { return LI; }

  /// Calculate the natural loop information for a given function.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Verify the LoopInfo computed by this pass.
  void verifyAnalysis() const override;

  /// Release the LoopInfo owned by this pass.
  void releaseMemory() override { LI.releaseMemory(); }

  /// Print the LoopInfo computed by this pass.
  /// @param O Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &O, const Module *M = nullptr) const override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Function to print a loop's contents as LLVM's text IR assembly.
/// @param L Loop to print.
/// @param OS Output stream.
/// @param Banner Optional banner printed before the loop.
LLVM_ABI void printLoop(const Loop &L, raw_ostream &OS,
                        const std::string &Banner = "");

/// Find and return the loop attribute node for the attribute @p Name in
/// @p LoopID. Return nullptr if there is no such attribute.
/// @param LoopID Loop-ID metadata node to search.
/// @param Name Attribute name to look up.
/// @return The attribute MDNode, or nullptr if not found.
LLVM_ABI MDNode *findOptionMDForLoopID(MDNode *LoopID, StringRef Name);

/// Find string metadata for a loop.
///
/// Returns the MDNode where the first operand is the metadata's name. The
/// following operands are the metadata's values. If no metadata with @p Name is
/// found, return nullptr.
/// @param TheLoop Loop whose metadata is searched.
/// @param Name Attribute name to look up.
/// @return The attribute MDNode, or nullptr if not found.
LLVM_ABI MDNode *findOptionMDForLoop(const Loop *TheLoop, StringRef Name);

/// Return the optional boolean value of loop attribute \p Name on \p TheLoop.
/// @param TheLoop Loop whose attribute is queried.
/// @param Name Boolean attribute name to look up.
/// @return The attribute value when set, otherwise std::nullopt.
LLVM_ABI std::optional<bool> getOptionalBoolLoopAttribute(const Loop *TheLoop,
                                                          StringRef Name);

/// Returns true if Name is applied to TheLoop and enabled.
/// @param TheLoop Loop whose attribute is queried.
/// @param Name Boolean attribute name to look up.
/// @return True if \p Name is present and enabled on \p TheLoop.
LLVM_ABI bool getBooleanLoopAttribute(const Loop *TheLoop, StringRef Name);

/// Find named metadata for a loop with an integer value.
/// @param TheLoop Loop whose attribute is queried.
/// @param Name Integer attribute name to look up.
/// @return The integer attribute value when set, otherwise std::nullopt.
LLVM_ABI std::optional<int> getOptionalIntLoopAttribute(const Loop *TheLoop,
                                                        StringRef Name);

/// Find named metadata for a loop with an integer value. Return \p Default if
/// not set.
/// @param TheLoop Loop whose attribute is queried.
/// @param Name Integer attribute name to look up.
/// @param Default Value returned when the attribute is unset.
/// @return The integer attribute value, or \p Default if unset.
LLVM_ABI int getIntLoopAttribute(const Loop *TheLoop, StringRef Name,
                                 int Default = 0);

/// Find string metadata for loop
///
/// If it has a value (e.g. {"llvm.distribute", 1} return the value as an
/// operand or null otherwise.  If the string metadata is not found return
/// Optional's not-a-value.
/// @param TheLoop Loop whose metadata is searched.
/// @param Name String metadata name to look up.
/// @return The value operand when present, otherwise std::nullopt.
LLVM_ABI std::optional<const MDOperand *>
findStringMetadataForLoop(const Loop *TheLoop, StringRef Name);

/// Find the convergence heart of the loop.
/// @param TheLoop Loop whose convergence heart is requested.
/// @return The convergence heart call, or nullptr if none.
LLVM_ABI CallBase *getLoopConvergenceHeart(const Loop *TheLoop);

/// Return true if the loop itself has the mustprogress attribute.
///
/// Note: Most consumers probably want "isMustProgress" which checks
/// the containing function attribute too.
/// @param L Loop to query for the mustprogress attribute.
/// @return True if \p L has the mustprogress attribute.
LLVM_ABI bool hasMustProgress(const Loop *L);

/// Return true if this loop can be assumed to make progress.  (i.e. can't
/// be infinite without side effects without also being undefined)
/// @param L Loop to test for mustprogress.
/// @return True if \p L can be assumed to make progress.
LLVM_ABI bool isMustProgress(const Loop *L);

/// Return true if this loop can be assumed to run for a finite number of
/// iterations.
/// @param L Loop to test for a finite trip count.
/// @return True if \p L can be assumed to have a finite trip count.
LLVM_ABI bool isFinite(const Loop *L);

/// Return whether an MDNode might represent an access group.
///
/// Access group metadata nodes have to be distinct and empty. Being
/// always-empty ensures that it never needs to be changed (which -- because
/// MDNodes are designed immutable -- would require creating a new MDNode). Note
/// that this is not a sufficient condition: not every distinct and empty NDNode
/// is representing an access group.
/// @param AccGroup Metadata node to test as a possible access group.
/// @return True if \p AccGroup might represent an access group.
LLVM_ABI bool isValidAsAccessGroup(MDNode *AccGroup);

/// Create a new LoopID after the loop has been transformed.
///
/// This can be used when no follow-up loop attributes are defined
/// (llvm::makeFollowupLoopID returning None) to stop transformations to be
/// applied again.
///
/// @param Context        The LLVMContext in which to create the new LoopID.
/// @param OrigLoopID     The original LoopID; can be nullptr if the original
///                       loop has no LoopID.
/// @param RemovePrefixes Remove all loop attributes that have these prefixes.
///                       Use to remove metadata of the transformation that has
///                       been applied.
/// @param AddAttrs       Add these loop attributes to the new LoopID.
///
/// @return A new LoopID that can be applied using Loop::setLoopID().
LLVM_ABI llvm::MDNode *
makePostTransformationMetadata(llvm::LLVMContext &Context, MDNode *OrigLoopID,
                               llvm::ArrayRef<llvm::StringRef> RemovePrefixes,
                               llvm::ArrayRef<llvm::MDNode *> AddAttrs);
} // namespace llvm

#endif
