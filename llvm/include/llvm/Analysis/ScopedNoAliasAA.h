//===- ScopedNoAliasAA.h - Scoped No-Alias Alias Analysis -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the interface for a metadata-based scoped no-alias analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCOPEDNOALIASAA_H
#define LLVM_ANALYSIS_SCOPEDNOALIASAA_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class FenceInst;
class Function;
class MDNode;
class MemoryLocation;

/// A simple AA result which uses scoped-noalias metadata to answer queries.
class ScopedNoAliasAAResult : public AAResultBase {
public:
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

  /// Query whether two locations may alias using scoped-noalias metadata.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI static AliasResult alias(const MemoryLocation &LocA,
                                    const MemoryLocation &LocB);
  /// Query whether two memory locations may alias using query state \p AAQI.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI);
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
  /// Return ModRef info for a fence against location \p Loc.
  /// @param F Fence instruction whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the fence.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the fence may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const FenceInst *F,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);

  /// Collect the scoped domains referenced by noalias metadata \p NoAlias.
  /// @param NoAlias Noalias metadata listing scopes, or null.
  /// @param Domains Set that receives the domains; must be empty on entry.
  LLVM_ABI static void
  collectScopedDomains(const MDNode *NoAlias,
                       SmallPtrSetImpl<const MDNode *> &Domains);

  /// Return whether \p Scopes may alias given noalias metadata \p NoAlias.
  ///
  /// Returns false when, for some domain, the noalias scopes are a superset of
  /// the alias scopes in that domain. Returns true if either argument is null
  /// or no such domain exists.
  /// @param Scopes Alias-scope metadata attached to an access, or null.
  /// @param NoAlias Noalias-scope metadata attached to another access, or null.
  /// @return False if noalias scopes dominate alias scopes in some domain;
  /// true otherwise.
  LLVM_ABI static bool mayAliasInScopes(const MDNode *Scopes,
                                        const MDNode *NoAlias);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class ScopedNoAliasAA : public AnalysisInfoMixin<ScopedNoAliasAA> {
  friend AnalysisInfoMixin<ScopedNoAliasAA>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  using Result = ScopedNoAliasAAResult;

  /// Run scoped no-alias analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return A ScopedNoAliasAAResult for \p F.
  LLVM_ABI ScopedNoAliasAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the ScopedNoAliasAAResult object.
class LLVM_ABI ScopedNoAliasAAWrapperPass : public ImmutablePass {
  std::unique_ptr<ScopedNoAliasAAResult> Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a ScopedNoAliasAAWrapperPass.
  ScopedNoAliasAAWrapperPass();

  /// Return the ScopedNoAliasAAResult object.
  /// @return Reference to the cached ScopedNoAliasAAResult.
  ScopedNoAliasAAResult &getResult() { return *Result; }
  /// Return the ScopedNoAliasAAResult object.
  /// @return Const reference to the cached ScopedNoAliasAAResult.
  const ScopedNoAliasAAResult &getResult() const { return *Result; }

  /// Create the ScopedNoAliasAAResult for module \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis pass does not modify the module.
  bool doInitialization(Module &M) override;
  /// Release the cached ScopedNoAliasAAResult after the module is processed.
  /// @param M Module whose analysis state is being finalized.
  /// @return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c ScopedNoAliasAAWrapperPass.
///
/// This pass implements metadata-based scoped noalias analysis.
/// @return A new ImmutablePass that provides ScopedNoAliasAAResult.
LLVM_ABI ImmutablePass *createScopedNoAliasAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCOPEDNOALIASAA_H
