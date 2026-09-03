//===- GlobalsModRef.h - Simple Mod/Ref AA for Globals ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for a simple mod/ref and alias analysis over globals.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GLOBALSMODREF_H
#define LLVM_ANALYSIS_GLOBALSMODREF_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <list>

namespace llvm {
class CallGraph;
class Function;

/// An alias analysis result set for globals.
///
/// This focuses on handling aliasing properties of globals and interprocedural
/// function call mod/ref information.
class GlobalsAAResult : public AAResultBase {
  class FunctionInfo;

  const DataLayout &DL;
  std::function<const TargetLibraryInfo &(Function &F)> GetTLI;

  /// The globals that do not have their addresses taken.
  SmallPtrSet<const GlobalValue *, 8> NonAddressTakenGlobals;

  /// Are there functions with local linkage that may modify globals.
  bool UnknownFunctionsWithLocalLinkage = false;

  /// IndirectGlobals - The memory pointed to by this global is known to be
  /// 'owned' by the global.
  SmallPtrSet<const GlobalValue *, 8> IndirectGlobals;

  /// AllocsForIndirectGlobals - If an instruction allocates memory for an
  /// indirect global, this map indicates which one.
  DenseMap<const Value *, const GlobalValue *> AllocsForIndirectGlobals;

  /// For each function, keep track of what globals are modified or read.
  DenseMap<const Function *, FunctionInfo> FunctionInfos;

  /// A map of functions to SCC. The SCCs are described by a simple integer
  /// ID that is only useful for comparing for equality (are two functions
  /// in the same SCC or not?)
  DenseMap<const Function *, unsigned> FunctionToSCCMap;

  /// Handle to clear this analysis on deletion of values.
  struct LLVM_ABI DeletionCallbackHandle final : CallbackVH {
    GlobalsAAResult *GAR;
    std::list<DeletionCallbackHandle>::iterator I;

    DeletionCallbackHandle(GlobalsAAResult &GAR, Value *V)
        : CallbackVH(V), GAR(&GAR) {}

    void deleted() override;
  };

  /// List of callbacks for globals being tracked by this analysis. Note that
  /// these objects are quite large, but we only anticipate having one per
  /// global tracked by this analysis. There are numerous optimizations we
  /// could perform to the memory utilization here if this becomes a problem.
  std::list<DeletionCallbackHandle> Handles;

  explicit GlobalsAAResult(
      const DataLayout &DL,
      std::function<const TargetLibraryInfo &(Function &F)> GetTLI);

  friend struct RecomputeGlobalsAAPass;

public:
  /// Move-construct a GlobalsAAResult from \p Arg.
  /// @param Arg GlobalsAAResult to move from.
  LLVM_ABI GlobalsAAResult(GlobalsAAResult &&Arg);
  /// Destroy this GlobalsAAResult and its tracked callbacks.
  LLVM_ABI ~GlobalsAAResult();

  /// Handle invalidation events in the new pass manager.
  /// @param M Module whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Build a GlobalsAAResult by analyzing module \p M.
  /// @param M Module to analyze.
  /// @param GetTLI Callback returning TargetLibraryInfo for a function.
  /// @param CG Call graph of the module.
  /// @return GlobalsAAResult computed for \p M.
  LLVM_ABI static GlobalsAAResult
  analyzeModule(Module &M,
                std::function<const TargetLibraryInfo &(Function &F)> GetTLI,
                CallGraph &CG);

  //------------------------------------------------
  // Implement the AliasAnalysis API
  //
  /// Query whether two memory locations may alias using query state \p AAQI.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return Alias result for \p LocA and \p LocB.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI);

  /// Inherit getModRefInfo overloads from AAResultBase.
  using AAResultBase::getModRefInfo;
  /// Return ModRef info for call \p Call against location \p Loc.
  /// @param Call Call site whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the call.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info for \p Call against \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);

  /// Inherit getMemoryEffects overloads from AAResultBase.
  using AAResultBase::getMemoryEffects;
  /// Return the memory effects of calling function \p F.
  ///
  /// For use when the call site is not known; returns the most generic
  /// behavior of this function.
  /// @param F Function whose memory effects are queried.
  /// @return The memory effects of calling \p F.
  LLVM_ABI MemoryEffects getMemoryEffects(const Function *F);

private:
  FunctionInfo *getFunctionInfo(const Function *F);

  void AnalyzeGlobals(Module &M);
  void AnalyzeCallGraph(CallGraph &CG, Module &M);
  bool AnalyzeUsesOfPointer(Value *V,
                            SmallPtrSetImpl<Function *> *Readers = nullptr,
                            SmallPtrSetImpl<Function *> *Writers = nullptr,
                            GlobalValue *OkayStoreDest = nullptr);
  bool AnalyzeIndirectGlobalMemory(GlobalVariable *GV);
  void CollectSCCMembership(CallGraph &CG);

  bool isNonEscapingGlobalNoAlias(const GlobalValue *GV, const Value *V,
                                  const Instruction *CtxI);
  ModRefInfo getModRefInfoForArgument(const CallBase *Call,
                                      const GlobalValue *GV, AAQueryInfo &AAQI);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class GlobalsAA : public AnalysisInfoMixin<GlobalsAA> {
  friend AnalysisInfoMixin<GlobalsAA>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  typedef GlobalsAAResult Result;

  /// Run globals alias analysis on module \p M.
  /// @param M Module to analyze.
  /// @param AM Module analysis manager providing dependencies.
  /// @return GlobalsAAResult for \p M.
  LLVM_ABI GlobalsAAResult run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that clears and recomputes a cached GlobalsAA result.
struct RecomputeGlobalsAAPass : OptionalPassInfoMixin<RecomputeGlobalsAAPass> {
  /// Recompute GlobalsAA for module \p M if a cached result exists.
  /// @param M Module whose GlobalsAA should be refreshed.
  /// @param AM Module analysis manager providing GlobalsAA and CallGraph.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the GlobalsAAResult object.
class LLVM_ABI GlobalsAAWrapperPass : public ModulePass {
  std::unique_ptr<GlobalsAAResult> Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a GlobalsAAWrapperPass.
  GlobalsAAWrapperPass();

  /// Return the GlobalsAAResult computed for the last module.
  /// @return The cached GlobalsAAResult.
  GlobalsAAResult &getResult() { return *Result; }
  /// Return the GlobalsAAResult computed for the last module.
  /// @return The cached GlobalsAAResult.
  const GlobalsAAResult &getResult() const { return *Result; }

  /// Compute GlobalsAAResult for module \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis pass does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the cached GlobalsAAResult after the module is processed.
  /// @param M Module whose analysis state is being finalized.
  /// @return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c GlobalsAAWrapperPass.
///
/// This pass provides alias and mod/ref info for global values that do not
/// have their addresses taken.
/// @return A new GlobalsAAWrapperPass instance.
LLVM_ABI ModulePass *createGlobalsAAWrapperPass();
}

#endif
