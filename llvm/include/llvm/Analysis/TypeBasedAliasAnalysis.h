//===- TypeBasedAliasAnalysis.h - Type-Based Alias Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the interface for a metadata-based TBAA. See the source file for
/// details on the algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
#define LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class CallBase;
class Function;
class MDNode;
class MemoryLocation;

/// A simple AA result that uses TBAA metadata to answer queries.
class TypeBasedAAResult : public AAResultBase {
  /// True if type sanitizer is enabled. When TypeSanitizer is used, don't use
  /// TBAA information for alias analysis as  this might cause us to remove
  /// memory accesses that we need to verify at runtime.
  bool UsingTypeSanitizer;

public:
  /// Construct a TypeBasedAAResult.
  /// @param UsingTypeSanitizer Whether type sanitizer is enabled.
  TypeBasedAAResult(bool UsingTypeSanitizer)
      : UsingTypeSanitizer(UsingTypeSanitizer) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  /// @param Fn Function whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return False; this result is never invalidated.
  bool invalidate(Function &Fn, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }

  /// Query whether two memory locations may alias using TBAA metadata.
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
  /// Return a ModRef bitmask for a memory location using TBAA metadata.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param AAQI Query state and caches for this query.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  LLVM_ABI ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI, bool IgnoreLocals);

  /// Return memory effects for call site \p Call using TBAA metadata.
  /// @param Call Call site whose memory effects are queried.
  /// @param AAQI Query state and caches for this query.
  /// @return Memory effects of the call site.
  LLVM_ABI MemoryEffects getMemoryEffects(const CallBase *Call,
                                          AAQueryInfo &AAQI);
  /// Return memory effects for function \p F using TBAA metadata.
  /// @param F Function whose memory effects are queried.
  /// @return Memory effects of the function.
  LLVM_ABI MemoryEffects getMemoryEffects(const Function *F);
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
  /// Return ModRef info between two call sites using TBAA metadata.
  /// @param Call1 First call site.
  /// @param Call2 Second call site.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing shared memory access between the call sites.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call1,
                                    const CallBase *Call2, AAQueryInfo &AAQI);

private:
  bool Aliases(const MDNode *A, const MDNode *B) const;

  /// Returns true if TBAA metadata should be used, that is if TBAA is enabled
  /// and type sanitizer is not used.
  bool shouldUseTBAA() const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class TypeBasedAA : public AnalysisInfoMixin<TypeBasedAA> {
  friend AnalysisInfoMixin<TypeBasedAA>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  using Result = TypeBasedAAResult;

  /// Run type-based alias analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return A TypeBasedAAResult for the function.
  LLVM_ABI TypeBasedAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the TypeBasedAAResult object.
class LLVM_ABI TypeBasedAAWrapperPass : public ImmutablePass {
  std::unique_ptr<TypeBasedAAResult> Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a TypeBasedAAWrapperPass.
  TypeBasedAAWrapperPass();

  /// Return the TypeBasedAAResult for this pass.
  /// @return The TypeBasedAAResult owned by this pass.
  TypeBasedAAResult &getResult() { return *Result; }
  /// Return the TypeBasedAAResult for this pass.
  /// @return The TypeBasedAAResult owned by this pass.
  const TypeBasedAAResult &getResult() const { return *Result; }

  /// Initialize the TypeBasedAAResult for module \p M.
  /// @param M Module being initialized.
  /// @return False; this analysis pass does not modify the module.
  bool doInitialization(Module &M) override;
  /// Tear down the TypeBasedAAResult for module \p M.
  /// @param M Module being finalized.
  /// @return False; this analysis pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c TypeBasedAAWrapperPass.
///
/// This pass implements metadata-based type-based alias analysis.
/// @return A new TypeBasedAAWrapperPass instance.
LLVM_ABI ImmutablePass *createTypeBasedAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
