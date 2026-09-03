//===- BasicAliasAnalysis.h - Stateless, local Alias Analysis ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for LLVM's primary stateless and local alias analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BASICALIASANALYSIS_H
#define LLVM_ANALYSIS_BASICALIASANALYSIS_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <utility>

namespace llvm {

class AssumptionCache;
class DataLayout;
class DominatorTree;
class Function;
class GEPOperator;
class PHINode;
class SelectInst;
class TargetLibraryInfo;
class Value;

/// AA result for basic, local, and stateless alias analysis.
///
/// This is the AA result object for the basic, local, and stateless alias
/// analysis. It implements the AA query interface in an entirely stateless
/// manner. As one consequence, it is never invalidated due to IR changes.
/// While it does retain some storage, that is used as an optimization and not
/// to preserve information from query to query. However it does retain handles
/// to various other analyses and must be recomputed when those analyses are.
class BasicAAResult : public AAResultBase {
  const DataLayout &DL;
  const Function &F;
  const TargetLibraryInfo &TLI;
  AssumptionCache &AC;
  /// Use getDT() instead of accessing this member directly, in order to
  /// respect the AAQI.UseDominatorTree option.
  DominatorTree *DT_;

  DominatorTree *getDT(const AAQueryInfo &AAQI) const {
    return AAQI.UseDominatorTree ? DT_ : nullptr;
  }

public:
  /// Construct a BasicAAResult for function \p F.
  /// @param DL Data layout for the module.
  /// @param F Function being analyzed.
  /// @param TLI Target library info used by queries.
  /// @param AC Assumption cache for the function.
  /// @param DT Optional dominator tree for the function.
  BasicAAResult(const DataLayout &DL, const Function &F,
                const TargetLibraryInfo &TLI, AssumptionCache &AC,
                DominatorTree *DT = nullptr)
      : DL(DL), F(F), TLI(TLI), AC(AC), DT_(DT) {}

  /// Copy-construct a BasicAAResult from \p Arg.
  /// @param Arg BasicAAResult to copy from.
  BasicAAResult(const BasicAAResult &Arg)
      : AAResultBase(Arg), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI), AC(Arg.AC),
        DT_(Arg.DT_) {}
  /// Move-construct a BasicAAResult from \p Arg.
  /// @param Arg BasicAAResult to move from.
  BasicAAResult(BasicAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI),
        AC(Arg.AC), DT_(Arg.DT_) {}

  /// Handle invalidation events in the new pass manager.
  /// @param Fn Function whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return True if this result should be invalidated.
  LLVM_ABI bool invalidate(Function &Fn, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Query whether two memory locations may alias using query state \p AAQI.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI);

  /// Return whether \p Loc may alias errno at context \p CtxI.
  /// @param Loc Memory location that may alias errno.
  /// @param CtxI Context instruction for the errno query.
  /// @return An AliasResult indicating whether \p Loc may alias errno.
  LLVM_ABI AliasResult aliasErrno(const MemoryLocation &Loc,
                                  const Instruction *CtxI);

  /// Inherit getModRefInfo overloads from AAResultBase.
  using AAResultBase::getModRefInfo;
  /// Return ModRef info for call \p Call against location \p Loc.
  /// @param Call Call site whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the call.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the call may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);

  /// Return ModRef info between two call sites.
  /// @param Call1 First call site.
  /// @param Call2 Second call site.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing shared memory access between the call sites.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call1,
                                    const CallBase *Call2, AAQueryInfo &AAQI);

  /// Return a ModRef bitmask for a memory location.
  ///
  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref
  /// from the ModRef info based on the knowledge that the memory location
  /// points to constant and/or locally-invariant memory.
  ///
  /// If IgnoreLocals is true, then this method returns NoModRef for memory
  /// that points to a local alloca.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param AAQI Query state and caches for this query.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  LLVM_ABI ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI,
                                        bool IgnoreLocals = false);

  /// Get the location associated with a pointer argument of a callsite.
  /// @param Call Call whose argument ModRef info is queried.
  /// @param ArgIdx Zero-based index of the pointer argument.
  /// @return ModRef info describing how the argument may be accessed.
  LLVM_ABI ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Returns the behavior when calling the given call site.
  /// @param Call Call site whose memory effects are queried.
  /// @param AAQI Query state and caches for this query.
  /// @return Memory effects of the call site.
  LLVM_ABI MemoryEffects getMemoryEffects(const CallBase *Call,
                                          AAQueryInfo &AAQI);

  /// Returns the behavior when calling the given function. For use when the
  /// call site is not known.
  /// @param Fn Function whose memory effects are queried.
  /// @return Memory effects when calling the function.
  LLVM_ABI MemoryEffects getMemoryEffects(const Function *Fn);

private:
  struct DecomposedGEP;
  struct VariableGEPOffsetInfo;

  /// Tracks instructions visited by pointsToConstantMemory.
  SmallPtrSet<const Value *, 16> Visited;

  static DecomposedGEP
  DecomposeGEPExpression(const Value *V, const DataLayout &DL,
                         AssumptionCache *AC, DominatorTree *DT);

  /// Analyze the variable indices of a decomposed GEP, computing the GCD
  /// that each Scale*V term is a multiple of, and an approximate range of
  /// possible total offsets.
  VariableGEPOffsetInfo analyzeVariableOffsets(const DecomposedGEP &GEP,
                                               DominatorTree *DT);

  /// Try to determine the range of values for VarIndex such that
  /// VarIndex <= -MinAbsVarIndex || MinAbsVarIndex <= VarIndex, thus
  /// establishing a minimum absolute value of the variable offset.
  std::optional<APInt> computeMinAbsVarOffset(const DecomposedGEP &GEP,
                                              DominatorTree *DT,
                                              const AAQueryInfo &AAQI);

  /// A Heuristic for aliasGEP that searches for a constant offset
  /// between the variables.
  ///
  /// GetLinearExpression has some limitations, as generally zext(%x + 1)
  /// != zext(%x) + zext(1) if the arithmetic overflows. GetLinearExpression
  /// will therefore conservatively refuse to decompose these expressions.
  /// However, we know that, for all %x, zext(%x) != zext(%x + 1), even if
  /// the addition overflows.
  bool computeConstantOffsetHeuristic(const DecomposedGEP &GEP,
                                      LocationSize V1Size, LocationSize V2Size,
                                      AssumptionCache *AC, DominatorTree *DT,
                                      const AAQueryInfo &AAQI);

  bool isValueEqualInPotentialCycles(const Value *V1, const Value *V2,
                                     const AAQueryInfo &AAQI);

  void subtractDecomposedGEPs(DecomposedGEP &DestGEP,
                              const DecomposedGEP &SrcGEP,
                              const AAQueryInfo &AAQI);

  AliasResult aliasGEP(const GEPOperator *V1, LocationSize V1Size,
                       const Value *V2, LocationSize V2Size,
                       const Value *UnderlyingV1, const Value *UnderlyingV2,
                       AAQueryInfo &AAQI);

  AliasResult aliasPHI(const PHINode *PN, LocationSize PNSize,
                       const Value *V2, LocationSize V2Size, AAQueryInfo &AAQI);

  AliasResult aliasSelect(const SelectInst *SI, LocationSize SISize,
                          const Value *V2, LocationSize V2Size,
                          AAQueryInfo &AAQI);

  AliasResult aliasCheck(const Value *V1, LocationSize V1Size, const Value *V2,
                         LocationSize V2Size, AAQueryInfo &AAQI,
                         const Instruction *CtxI);

  AliasResult aliasCheckRecursive(const Value *V1, LocationSize V1Size,
                                  const Value *V2, LocationSize V2Size,
                                  AAQueryInfo &AAQI, const Value *O1,
                                  const Value *O2);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class BasicAA : public AnalysisInfoMixin<BasicAA> {
  friend AnalysisInfoMixin<BasicAA>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  using Result = BasicAAResult;

  /// Run basic alias analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return A BasicAAResult for \p F.
  LLVM_ABI BasicAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the BasicAAResult object.
class LLVM_ABI BasicAAWrapperPass : public FunctionPass {
  std::unique_ptr<BasicAAResult> Result;

  virtual void anchor();

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a BasicAAWrapperPass.
  BasicAAWrapperPass();

  /// Return the BasicAAResult computed for the last function.
  /// @return The BasicAAResult computed for the last function.
  BasicAAResult &getResult() { return *Result; }
  /// Return the BasicAAResult computed for the last function.
  /// @return The BasicAAResult computed for the last function.
  const BasicAAResult &getResult() const { return *Result; }

  /// Compute BasicAAResult for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c BasicAAWrapperPass.
/// @return A FunctionPass that provides BasicAAResult.
LLVM_ABI FunctionPass *createBasicAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_BASICALIASANALYSIS_H
